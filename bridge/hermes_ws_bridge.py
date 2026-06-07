#!/usr/bin/env python3
"""
OCDesktop ↔ Hermes WebSocket bridge.

Decision A: runs in-process with Hermes, sharing the real SessionDB and the
AIAgent runtime. Speaks the protocol in docs/HERMES_WS_PROTOCOL.md so the
OCDesktop C++ client's WsGateway is a drop-in for MockGateway.

Reads  : SessionDB.list_sessions_rich() / get_messages()
Writes : AIAgent.run_conversation(stream_delta_callback=...) for send+stream

Run standalone (for testing / lightweight deploy):
    HERMES_HOME=~/.hermes \
    OCDESKTOP_WS_TOKEN=secret \
    python -m bridge.hermes_ws_bridge --host 127.0.0.1 --port 8770

Bind to a Tailscale address in production; never 0.0.0.0 publicly.
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import hmac
import json
import logging
import os
import re
import time
import uuid
from typing import Any, Dict, List, Optional

logger = logging.getLogger("ocdesktop.ws_bridge")

PROTOCOL_VERSION = 1
SELF_PEER_ID = 1
SELF_NAME = "You"


def _ensure_hermes_on_path() -> None:
    """Make hermes-agent importable whether installed or run from a checkout."""
    try:
        import hermes_state  # noqa: F401
        return
    except ImportError:
        pass
    candidates = [
        os.environ.get("HERMES_AGENT_DIR"),
        os.path.expanduser("~/.hermes/hermes-agent"),
    ]
    import sys
    for c in candidates:
        if c and os.path.isdir(c) and c not in sys.path:
            sys.path.insert(0, c)


def session_id_to_peer_id(session_id: str) -> int:
    """Stable peer id from a session id string.

    tdesktop's PeerId/UserId only accepts bare user ids within a limited
    range (well under 2^48), so we mask the hash to 40 bits — ample space
    for negligible collisions across a few hundred sessions while staying
    inside the valid UserId range. Lower bit forced to 1 so it never
    collides with SELF_PEER_ID (1) and never lands on 0.
    """
    digest = hashlib.blake2b(session_id.encode("utf-8"), digest_size=8).digest()
    val = int.from_bytes(digest, "big")
    val &= (1 << 40) - 1
    val |= 1
    if val <= SELF_PEER_ID:
        val += 2
    return val


def _preview_title(sess: Dict[str, Any]) -> str:
    title = (sess.get("title") or "").strip()
    if title:
        return title
    preview = (sess.get("preview") or "").strip()
    if preview:
        return preview[:40]
    return sess.get("source") or sess.get("id", "session")[:8]


class HermesWsBridge:
    # Synthetic message ids for the live send/stream path live in a high band:
    # above any real DB message id (so they sort as newest) but safely under
    # INT32_MAX, because the C++ seeder narrows every id through MTP_int (32-bit).
    # A 64-bit id here overflows int32 and crashes the client on send.
    _SYNTH_ID_BASE = 1_000_000_000
    _INT32_MAX = (1 << 31) - 1

    def __init__(self, token: str, *, session_limit: int = 50):
        self._token = token
        self._session_limit = session_limit
        self._db = None
        # Optional id->display-name overrides for the household picker, parsed
        # from the OCDESKTOP_ROSTER env ("id:Name,id:Name"). Names that aren't
        # listed fall back to the raw user_id, so the client never hardcodes
        # who the members are — adding a person is one env edit, no rebuild.
        self._roster_names = self._parse_roster_names(
            os.environ.get("OCDESKTOP_ROSTER", ""))
        # peerId -> session_id, rebuilt on every sessions.list
        self._peer_to_session: Dict[int, str] = {}
        self._session_to_peer: Dict[str, int] = {}
        # Monotonic allocator for live send/stream message ids.
        self._synth_counter = self._SYNTH_ID_BASE
    def _next_synth_id(self) -> int:
        self._synth_counter += 1
        if self._synth_counter >= self._INT32_MAX:
            self._synth_counter = self._SYNTH_ID_BASE + 1
        return self._synth_counter

    def _last_message_ids(self, session_ids=None) -> Dict[str, int]:
        """Real last user/assistant message id per session, for dialog anchoring.

        Scoped to the given session_ids (the ones actually being listed) so we
        don't full-scan MAX(id) across the entire messages table — that grouped
        scan over all sessions took ~10s and stalled sessions.list.
        """
        db = self._session_db()
        base = ("SELECT session_id, MAX(id) AS top FROM messages "
                "WHERE role IN ('user','assistant')")
        if session_ids:
            ids = list(session_ids)
            placeholders = ",".join("?" * len(ids))
            rows = db._conn.execute(
                base + f" AND session_id IN ({placeholders}) GROUP BY session_id",
                ids,
            ).fetchall()
        else:
            rows = db._conn.execute(base + " GROUP BY session_id").fetchall()
        return {r[0]: int(r[1] or 0) for r in rows}

    # ----- lazy Hermes wiring -----
    def _session_db(self):
        if self._db is None:
            _ensure_hermes_on_path()
            from hermes_state import SessionDB
            self._db = SessionDB()
        return self._db

    def _create_agent(self, session_id: Optional[str], stream_delta_callback):
        _ensure_hermes_on_path()
        from run_agent import AIAgent
        from gateway.run import (
            _resolve_runtime_agent_kwargs,
            _resolve_gateway_model,
            _load_gateway_config,
            GatewayRunner,
        )
        from hermes_cli.tools_config import _get_platform_tools

        runtime_kwargs = _resolve_runtime_agent_kwargs()
        model = _resolve_gateway_model()
        user_config = _load_gateway_config()
        try:
            enabled_toolsets = sorted(_get_platform_tools(user_config, "api_server"))
        except Exception:
            enabled_toolsets = None
        try:
            fallback_model = GatewayRunner._load_fallback_model()
        except Exception:
            fallback_model = None
        try:
            reasoning_config = GatewayRunner._load_reasoning_config()
        except Exception:
            reasoning_config = None

        return AIAgent(
            model=model,
            **runtime_kwargs,
            max_iterations=int(os.getenv("HERMES_MAX_ITERATIONS", "90")),
            quiet_mode=True,
            verbose_logging=False,
            enabled_toolsets=enabled_toolsets,
            session_id=session_id,
            platform="api_server",
            stream_delta_callback=stream_delta_callback,
            session_db=self._session_db(),
            fallback_model=fallback_model,
            reasoning_config=reasoning_config,
            gateway_session_key=f"ocdesktop:{session_id}" if session_id else None,
        )

    # ----- protocol ops -----
    @staticmethod
    def _parse_roster_names(raw: str) -> Dict[str, str]:
        names: Dict[str, str] = {}
        for part in (raw or "").split(","):
            part = part.strip()
            if not part or ":" not in part:
                continue
            uid, _, label = part.partition(":")
            uid, label = uid.strip(), label.strip()
            if uid and label:
                names[uid] = label
        return names

    def roster(self) -> Dict[str, Any]:
        """Distinct household members derived from real session data.

        IDs come live from the sessions table (whoever actually has chats);
        display names come from the OCDESKTOP_ROSTER env override, falling back
        to the raw id. Nothing about who the members are is hardcoded in the
        client — adding/removing a person needs no rebuild, just data + one
        optional env line for the friendly name.
        """
        db = self._session_db()
        rows = db._conn.execute(
            "SELECT user_id, COUNT(*) AS n FROM sessions "
            "WHERE source != 'cron' AND user_id IS NOT NULL AND user_id != '' "
            "GROUP BY user_id ORDER BY n DESC"
        ).fetchall()
        users: List[Dict[str, Any]] = []
        for r in rows:
            uid = str(r[0])
            users.append({
                "id": uid,
                "label": self._roster_names.get(uid, uid),
                "sessions": int(r[1] or 0),
            })
        # Synthetic "System" member: everything that isn't a named household
        # member (legacy NULL-user sessions + any unnamed user_id). Lets the
        # picker expose a third button for non-James/non-Abi sessions.
        named_ids = set(self._roster_names.keys())
        sys_count = db._conn.execute(
            "SELECT COUNT(*) FROM sessions WHERE source != 'cron' AND "
            "(user_id IS NULL OR user_id = '' OR user_id NOT IN ({}))".format(
                ",".join("?" * len(named_ids)) or "''"),
            list(named_ids),
        ).fetchone()
        users.append({
            "id": "system",
            "label": "System",
            "sessions": int((sys_count or [0])[0] or 0),
        })
        return {"users": users}

    def list_sessions(self, user: Optional[str] = None) -> Dict[str, Any]:
        db = self._session_db()
        sessions = db.list_sessions_rich(
            limit=self._session_limit if not user else 5000,
            exclude_sources=["cron"],
            order_by_last_active=True,
        )
        if user == "system":
            named = set(self._roster_names.keys())
            sessions = [s for s in sessions
                        if str(s.get("user_id") or "") not in named]
            sessions = sessions[:self._session_limit]
        elif user:
            sessions = [s for s in sessions if str(s.get("user_id") or "") == str(user)]
            sessions = sessions[:self._session_limit]
        self._peer_to_session.clear()
        self._session_to_peer.clear()
        last_ids = self._last_message_ids([s["id"] for s in sessions])
        peers: List[Dict[str, Any]] = []
        dialogs: List[Dict[str, Any]] = []
        for s in sessions:
            sid = s["id"]
            pid = session_id_to_peer_id(sid)
            self._peer_to_session[pid] = sid
            self._session_to_peer[sid] = pid
            peers.append({
                "id": pid,
                "name": _preview_title(s),
                "username": (s.get("source") or "hermes"),
                "about": s.get("preview") or "",
                "isBot": True,
            })
            dialogs.append({
                "peerId": pid,
                "pinned": False,
                "archived": False,
                "unreadCount": 0,
                "topMessageId": last_ids.get(sid, 0),
            })
        return {"peers": peers, "dialogs": dialogs}

    def history(self, peer_id: int, limit: int) -> Dict[str, Any]:
        sid = self._peer_to_session.get(peer_id)
        if sid is None:
            raise KeyError("session not found")
        db = self._session_db()
        rows = db.get_messages(sid)
        out: List[Dict[str, Any]] = []
        for row in rows:
            role = row.get("role")
            if role not in ("user", "assistant"):
                continue
            content = row.get("content")
            if isinstance(content, list):
                content = " ".join(
                    p.get("text", "") for p in content if isinstance(p, dict)
                )
            if not content:
                continue
            out.append({
                "id": int(row.get("id", 0)),
                "peerId": peer_id,
                "fromId": SELF_PEER_ID if role == "user" else peer_id,
                "text": str(content),
                "date": int(row.get("created_at") or row.get("timestamp") or 0) or int(time.time()),
                "replyToMsgId": 0,
            })
        if limit and len(out) > limit:
            out = out[-limit:]
        return {"messages": out, "lastBrowserUrl": _scan_last_url(out)}

    def messages_after(self, peer_id: int, after_id: int) -> List[Dict[str, Any]]:
        """User/assistant messages for a peer with id > after_id, oldest-first.

        Source-agnostic: reads straight from the session DB, so it catches
        replies that arrived via Telegram, cron, or any proactive path — not
        just turns initiated through this bridge. Drives the live-push poller.
        """
        sid = self._peer_to_session.get(peer_id)
        if sid is None:
            return []
        db = self._session_db()
        rows = db.get_messages(sid)
        out: List[Dict[str, Any]] = []
        for row in rows:
            mid = int(row.get("id", 0))
            if mid <= after_id:
                continue
            role = row.get("role")
            if role not in ("user", "assistant"):
                continue
            content = row.get("content")
            if isinstance(content, list):
                content = " ".join(
                    p.get("text", "") for p in content if isinstance(p, dict)
                )
            if not content:
                continue
            out.append({
                "id": mid,
                "peerId": peer_id,
                "fromId": SELF_PEER_ID if role == "user" else peer_id,
                "text": str(content),
                "date": int(row.get("created_at") or row.get("timestamp") or 0) or int(time.time()),
            })
        out.sort(key=lambda m: m["id"])
        return out


# ----------------------------------------------------------------------
# aiohttp WebSocket server
# ----------------------------------------------------------------------

# Live connections registry: lets a `browser` op (sent by an external Hermes
# tool connecting as a ws client) fan a navigate command out to the desktop
# client(s). Each entry: {"user": str|None, "send": coro, "ws": ws}.
CONNECTIONS: "list[Dict[str, Any]]" = []


async def _broadcast_browser_navigate(peer_id: int, url: str,
                                      user: "str|None" = None,
                                      exclude=None) -> int:
    """Push browser.navigate to every live client (optionally user-scoped).

    Returns the number of clients the command was delivered to. The sending
    connection (exclude) is skipped so a control tool doesn't get its own push.
    """
    delivered = 0
    for conn in list(CONNECTIONS):
        if exclude is not None and conn is exclude:
            continue
        if user is not None and conn.get("user") not in (None, user):
            continue
        try:
            await conn["send"]({"op": "update", "kind": "browser.navigate",
                                "peerId": int(peer_id), "url": str(url)})
            delivered += 1
        except Exception:  # noqa: BLE001
            logger.exception("browser.navigate push failed")
    return delivered


def _scan_last_url(messages: "List[Dict[str, Any]]") -> str:
    """Return the most recent http(s) URL mentioned in a message list.

    Used so opening a conversation restores the embedded browser to the last
    URL the agent/user referenced there. Newest message wins.
    """
    pattern = re.compile(r"https?://[^\s<>\"')\]]+")
    for m in reversed(messages):
        found = pattern.findall(str(m.get("text", "")))
        if found:
            return found[-1].rstrip(".,;")
    return ""


def build_app(bridge: "HermesWsBridge"):
    from aiohttp import web, WSMsgType

    async def ws_handler(request: "web.Request") -> "web.WebSocketResponse":
        ws = web.WebSocketResponse(heartbeat=30)
        await ws.prepare(request)
        authed = False
        current_user = None
        loop = asyncio.get_running_loop()

        # Per-peer high-water mark of the last message id pushed to THIS client.
        # Seeded from sessions.list, advanced by both the live poller and the
        # send path so a message is never pushed twice.
        watermarks: Dict[int, int] = {}
        poller_task = None
        conn_entry: "Dict[str, Any]|None" = None

        async def send(obj: Dict[str, Any]) -> None:
            await ws.send_str(json.dumps(obj))

        async def poll_new_messages() -> None:
            """Push DB messages that appear after the watermark, any source.

            This is the fix for 'incoming messages don't show': replies that
            land via Telegram, cron, or any proactive path are written to the
            session DB but were never pushed. We watch the DB and emit
            message.new for each, so the open client stays live.
            """
            while not ws.closed:
                try:
                    for peer_id, wm in list(watermarks.items()):
                        fresh = await loop.run_in_executor(
                            None, bridge.messages_after, peer_id, wm)
                        for m in fresh:
                            await send({"op": "update", "kind": "message.new",
                                        "peerId": peer_id, "message": m})
                            if m["id"] > watermarks.get(peer_id, 0):
                                watermarks[peer_id] = m["id"]
                except Exception:  # noqa: BLE001
                    logger.exception("poll_new_messages tick failed")
                await asyncio.sleep(2)

        async for msg in ws:
            if msg.type != WSMsgType.TEXT:
                continue
            try:
                frame = json.loads(msg.data)
            except json.JSONDecodeError:
                await send({"ok": False, "error": "bad json"})
                continue
            op = frame.get("op")
            rid = frame.get("id")
            logger.info("recv op=%s id=%s peerId=%s authed=%s",
                        op, rid, frame.get("peerId"), authed)

            if op == "auth":
                if hmac.compare_digest(str(frame.get("token", "")), bridge._token):
                    authed = True
                    current_user = (str(frame.get("user")).strip()
                                    if frame.get("user") else None) or None
                    logger.info("auth ok user=%s", current_user)
                    conn_entry = {"user": current_user, "send": send, "ws": ws}
                    CONNECTIONS.append(conn_entry)
                    await send({"id": rid, "ok": True, "protocol": PROTOCOL_VERSION,
                                "result": {"self": {"id": SELF_PEER_ID, "name": SELF_NAME}}})
                else:
                    await send({"id": rid, "ok": False, "error": "auth failed"})
                    await ws.close(code=4001, message=b"auth failed")
                continue

            if not authed:
                await send({"id": rid, "ok": False, "error": "not authenticated"})
                continue

            try:
                if op == "roster":
                    await send({"id": rid, "ok": True, "result": bridge.roster()})
                elif op == "sessions.list":
                    result = bridge.list_sessions(current_user)
                    for d in result.get("dialogs", []):
                        watermarks[int(d["peerId"])] = int(d.get("topMessageId") or 0)
                    await send({"id": rid, "ok": True, "result": result})
                    if poller_task is None:
                        poller_task = asyncio.create_task(poll_new_messages())
                elif op == "history":
                    await send({"id": rid, "ok": True,
                                "result": bridge.history(int(frame["peerId"]), int(frame.get("limit", 50)))})
                elif op == "message.send":
                    await _handle_send(bridge, ws, send, loop, frame, rid, watermarks)
                elif op == "typing":
                    pass  # client-driven typing is a no-op server-side for now
                elif op == "browser":
                    peer_id = frame.get("peerId")
                    if peer_id is None and frame.get("session"):
                        for pid, sid in bridge._peer_to_session.items():
                            if sid == frame.get("session"):
                                peer_id = pid
                                break
                    if peer_id is None:
                        await send({"id": rid, "ok": False,
                                    "error": "peerId or known session required"})
                    else:
                        n = await _broadcast_browser_navigate(
                            int(peer_id), str(frame.get("url", "")),
                            frame.get("user"), exclude=conn_entry)
                        await send({"id": rid, "ok": True,
                                    "result": {"delivered": n}})
                elif op == "peer.full":
                    sid = bridge._peer_to_session.get(int(frame["peerId"]))
                    await send({"id": rid, "ok": bool(sid),
                                "result": {"id": frame.get("peerId"), "name": sid or "", "isBot": True}})
                else:
                    await send({"id": rid, "ok": False, "error": "unknown op"})
            except KeyError as e:
                await send({"id": rid, "ok": False, "error": str(e) or "not found"})
            except Exception as e:  # noqa: BLE001
                logger.exception("op %s failed", op)
                await send({"id": rid, "ok": False, "error": f"internal: {e}"})

        if poller_task is not None:
            poller_task.cancel()
        if conn_entry is not None and conn_entry in CONNECTIONS:
            CONNECTIONS.remove(conn_entry)
        return ws

    async def health_handler(request: "web.Request") -> "web.Response":
        return web.json_response({"status": "ok"})

    app = web.Application()
    app.router.add_get("/ocdesktop", ws_handler)
    app.router.add_get("/health", health_handler)
    return app


async def _handle_send(bridge, ws, send, loop, frame, rid, watermarks=None) -> None:
    """message.send → ack the user turn, then stream the assistant reply."""
    peer_id = int(frame["peerId"])
    text = str(frame.get("text", ""))
    sid = bridge._peer_to_session.get(peer_id)
    logger.info("message.send peerId=%s sid=%s text=%r", peer_id, sid, text[:80])
    if sid is None:
        logger.warning("message.send: no session for peerId=%s (have %s peers mapped)",
                       peer_id, len(bridge._peer_to_session))
        await send({"id": rid, "ok": False, "error": "session not found"})
        return

    now = int(time.time())
    user_msg_id = bridge._next_synth_id()
    await send({"id": rid, "ok": True, "result": {"message": {
        "id": user_msg_id, "peerId": peer_id, "fromId": SELF_PEER_ID,
        "text": text, "date": now,
    }}})

    reply_msg_id = bridge._next_synth_id()
    # build 5's client update handler edits the bubble in place: stream.start
    # creates an empty bubble, each stream.delta REPLACES its text (setText), and
    # message.new finalises it — all sharing reply_msg_id, no duplicates. Because
    # the client replaces (not appends), we stream the CUMULATIVE text each frame.
    await send({"op": "update", "kind": "typing", "peerId": peer_id, "typing": True})
    await send({"op": "update", "kind": "stream.start", "peerId": peer_id, "msgId": reply_msg_id, "date": now})

    acc = {"text": ""}

    def on_delta(delta: str) -> None:
        if not delta:
            return
        acc["text"] += delta
        fut = asyncio.run_coroutine_threadsafe(
            send({"op": "update", "kind": "stream.delta",
                  "peerId": peer_id, "msgId": reply_msg_id, "text": acc["text"]}),
            loop,
        )
        try:
            fut.result(timeout=5)
        except Exception:
            pass

    def _run() -> str:
        agent = bridge._create_agent(session_id=sid, stream_delta_callback=on_delta)
        result = agent.run_conversation(user_message=text, task_id=sid or str(uuid.uuid4()))
        return (result or {}).get("final_response", "") if isinstance(result, dict) else str(result)

    try:
        final_text = await loop.run_in_executor(None, _run)
        logger.info("message.send done peerId=%s reply_len=%s", peer_id, len(final_text or ""))
    except Exception as e:  # noqa: BLE001
        logger.exception("agent run failed for peerId=%s", peer_id)
        await send({"op": "update", "kind": "typing", "peerId": peer_id, "typing": False})
        await send({"op": "update", "kind": "message.new", "peerId": peer_id, "message": {
            "id": reply_msg_id, "peerId": peer_id, "fromId": peer_id,
            "text": f"⚠️ agent error: {e}", "date": int(time.time()),
        }})
        return

    await send({"op": "update", "kind": "message.new", "peerId": peer_id, "message": {
        "id": reply_msg_id, "peerId": peer_id, "fromId": peer_id,
        "text": final_text, "date": int(time.time()),
    }})
    await send({"op": "update", "kind": "typing", "peerId": peer_id, "typing": False})

    # The agent persisted this turn to the DB under REAL ids. We already streamed
    # it via synthetic ids, so jump the watermark past the DB max to stop the
    # live poller re-pushing the same turn as duplicate bubbles.
    if watermarks is not None:
        try:
            persisted = await loop.run_in_executor(
                None, bridge.messages_after, peer_id, watermarks.get(peer_id, 0))
            if persisted:
                watermarks[peer_id] = max(m["id"] for m in persisted)
        except Exception:  # noqa: BLE001
            logger.exception("watermark advance failed for peerId=%s", peer_id)


def main() -> None:
    parser = argparse.ArgumentParser(description="OCDesktop ↔ Hermes WS bridge")
    parser.add_argument("--host", default=os.getenv("OCDESKTOP_WS_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("OCDESKTOP_WS_PORT", "8770")))
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    token = os.getenv("OCDESKTOP_WS_TOKEN", "")
    if not token:
        raise SystemExit("OCDESKTOP_WS_TOKEN must be set")

    from aiohttp import web
    bridge = HermesWsBridge(token=token)
    app = build_app(bridge)
    logger.info("OCDesktop WS bridge on ws://%s:%d/ocdesktop", args.host, args.port)
    web.run_app(app, host=args.host, port=args.port, print=None)


if __name__ == "__main__":
    main()
