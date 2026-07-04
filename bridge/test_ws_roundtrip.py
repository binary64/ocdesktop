#!/usr/bin/env python3
"""End-to-end WS round-trip test for the OCDesktop Hermes bridge.

Starts the real aiohttp server (real SessionDB reads), connects a websockets
client, and exercises: auth (good+bad), sessions.list, history, and the
streaming send path with a STUBBED agent (no real LLM call / no cost).
"""
import asyncio
import json
import os
import sys

sys.path.insert(0, "/mnt/arthur/clawd/projects/ocdesktop/bridge")
os.environ.setdefault("HERMES_HOME", "/mnt/arthur/.hermes")

import hermes_ws_bridge as B
import websockets
from aiohttp import web

TOKEN = "verify-token-123"
PORT = 8771


async def run():
    bridge = B.HermesWsBridge(token=TOKEN)

    # Stub the agent so message.send streams without a real LLM call.
    def fake_create_agent(session_id, stream_delta_callback):
        class _Stub:
            def run_conversation(self, user_message, task_id=None):
                for chunk in ["Hello", ", ", "Alice", "."]:
                    stream_delta_callback(chunk)
                return {"final_response": "Hello, Alice."}
        return _Stub()
    bridge._create_agent = fake_create_agent

    app = B.build_app(bridge)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", PORT)
    await site.start()

    url = f"ws://127.0.0.1:{PORT}/ocdesktop"
    results = {}
    try:
        # --- bad auth closes ---
        async with websockets.connect(url) as ws:
            await ws.send(json.dumps({"id": 1, "op": "auth", "token": "wrong"}))
            resp = json.loads(await ws.recv())
            results["bad_auth_rejected"] = (resp.get("ok") is False)

        # --- good path ---
        async with websockets.connect(url) as ws:
            await ws.send(json.dumps({"id": 1, "op": "auth", "token": TOKEN}))
            auth = json.loads(await ws.recv())
            results["auth_ok"] = auth.get("ok") is True
            results["protocol"] = auth.get("protocol")
            results["self_id"] = auth["result"]["self"]["id"]

            # unauthed op before auth on a fresh conn
            async with websockets.connect(url) as ws2:
                await ws2.send(json.dumps({"id": 9, "op": "sessions.list"}))
                r = json.loads(await ws2.recv())
                results["unauthed_blocked"] = (r.get("ok") is False)

            await ws.send(json.dumps({"id": 2, "op": "sessions.list"}))
            sl = json.loads(await ws.recv())
            results["sessions_count"] = len(sl["result"]["peers"])
            first_peer = sl["result"]["peers"][0]["id"]

            await ws.send(json.dumps({"id": 3, "op": "history", "peerId": first_peer, "limit": 10}))
            h = json.loads(await ws.recv())
            results["history_count"] = len(h["result"]["messages"])

            # --- streaming send ---
            await ws.send(json.dumps({"id": 4, "op": "message.send", "peerId": first_peer, "text": "hi"}))
            frames = []
            # ack + typing + stream.start + 4 deltas + message.new + typing = 9 frames
            for _ in range(9):
                frames.append(json.loads(await asyncio.wait_for(ws.recv(), timeout=10)))
            kinds = [f.get("kind") or ("ack" if f.get("id") == 4 else f.get("op")) for f in frames]
            results["send_ack"] = frames[0].get("ok") is True and frames[0]["result"]["message"]["fromId"] == 1
            results["frame_sequence"] = kinds
            deltas = [f["text"] for f in frames if f.get("kind") == "stream.delta"]
            results["streamed_text"] = "".join(deltas)
            finals = [f for f in frames if f.get("kind") == "message.new"]
            results["final_text"] = finals[0]["message"]["text"] if finals else None
            results["final_fromId_is_peer"] = bool(finals) and finals[0]["message"]["fromId"] == first_peer

            # unknown op
            await ws.send(json.dumps({"id": 5, "op": "bogus"}))
            u = json.loads(await ws.recv())
            results["unknown_op_handled"] = (u.get("ok") is False and "unknown" in u.get("error", ""))
    finally:
        await runner.cleanup()

    print(json.dumps(results, indent=2))
    # assertions
    ok = (
        results["bad_auth_rejected"] and results["auth_ok"] and results["unauthed_blocked"]
        and results["protocol"] == 1 and results["self_id"] == 1
        and results["sessions_count"] > 0 and results["send_ack"]
        and results["streamed_text"] == "Hello, Alice." and results["final_text"] == "Hello, Alice."
        and results["final_fromId_is_peer"] and results["unknown_op_handled"]
        and results["frame_sequence"] == [
            "ack", "typing", "stream.start", "stream.delta", "stream.delta",
            "stream.delta", "stream.delta", "message.new", "typing",
        ]
    )
    print("\nALL CHECKS PASSED" if ok else "\nFAILURES PRESENT")
    sys.exit(0 if ok else 1)


asyncio.run(run())
