import asyncio, aiohttp, time, random, json

URL = "http://127.0.0.1:8080/ingest"

async def worker(n):
    async with aiohttp.ClientSession() as s:
        for i in range(1000):
            payload = {
                "sensor_id": f"s{n}-{i%100}",
                "ts": int(time.time()),
                "metrics": {"temperature": random.uniform(15,30), "humidity": random.uniform(40,70)}
            }
            async with s.post(URL, json=payload) as r:
                await r.text()

async def main():
    await asyncio.gather(*(worker(i) for i in range(200)))

asyncio.run(main())
