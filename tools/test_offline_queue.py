"""Host-side fault simulation for the ESP append-only queue protocol."""
from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
import zlib
import random
from pathlib import Path

HEADER = struct.Struct('<II')  # payload length, crc32


class Journal:
    def __init__(self, path: Path):
        self.path = path

    def append(self, record: dict, partial: int | None = None):
        payload = json.dumps(record, sort_keys=True, separators=(',', ':')).encode()
        framed = HEADER.pack(len(payload), zlib.crc32(payload)) + payload
        with self.path.open('ab') as stream:
            stream.write(framed if partial is None else framed[:partial])
            stream.flush()
            os.fsync(stream.fileno())

    def load(self):
        records = []
        if not self.path.exists():
            return records
        with self.path.open('rb') as stream:
            while True:
                header = stream.read(HEADER.size)
                if not header:
                    break
                if len(header) != HEADER.size:
                    break
                length, checksum = HEADER.unpack(header)
                payload = stream.read(length)
                if len(payload) != length or zlib.crc32(payload) != checksum:
                    break
                records.append(json.loads(payload))
        return records

    def pending(self):
        rows = self.load()
        terminal = {r['id'] for r in rows if r['kind'] in {'ack', 'reject'}}
        return [r for r in rows if r['kind'] == 'event' and r['id'] not in terminal]


class OfflineQueueSimulation(unittest.TestCase):
    def test_enqueue_reboot_batch_timeout_duplicate_reject_and_corrupt_tail(self):
        with tempfile.TemporaryDirectory() as folder:
            journal = Journal(Path(folder) / 'offline.log')
            for sequence in range(1, 101):
                journal.append({'kind': 'event', 'id': f'KIOSK-01-{sequence}', 'sequence': sequence})
            self.assertEqual([x['sequence'] for x in Journal(journal.path).pending()], list(range(1, 101)))

            # Batch 1 committed server-side but its response was lost. Reboot
            # preserves all 100; retry ACKs the exact same IDs.
            retry_ids = [x['id'] for x in Journal(journal.path).pending()[:10]]
            for event_id in retry_ids:
                journal.append({'kind': 'ack', 'id': event_id})
            self.assertEqual(len(Journal(journal.path).pending()), 90)

            rejected = Journal(journal.path).pending()[0]['id']
            journal.append({'kind': 'reject', 'id': rejected, 'reason': 'BUSINESS_REJECT'})
            self.assertEqual(len(Journal(journal.path).pending()), 89)

            for row in list(Journal(journal.path).pending()):
                journal.append({'kind': 'ack', 'id': row['id']})
            self.assertEqual(Journal(journal.path).pending(), [])

            # Power loss midway through the final append must not corrupt any
            # prior record or resurrect an acknowledged event.
            journal.append({'kind': 'event', 'id': 'TORN', 'sequence': 101}, partial=11)
            self.assertEqual(Journal(journal.path).pending(), [])

    def test_partial_batch_lost_response_is_idempotent(self):
        """Server commits 1-6, response disappears, then all ten are replayed."""
        events = [f'KIOSK-01-{n}' for n in range(1, 11)]
        committed: set[str] = set()
        for event_id in events[:6]:
            committed.add(event_id)
        statuses = []
        for event_id in events:
            status = 'duplicate' if event_id in committed else 'accepted'
            committed.add(event_id)
            statuses.append(status)
        self.assertEqual(statuses[:6], ['duplicate'] * 6)
        self.assertEqual(statuses[6:], ['accepted'] * 4)
        self.assertEqual(len(committed), 10)

    def test_ack_then_power_loss_before_local_mark(self):
        with tempfile.TemporaryDirectory() as folder:
            journal = Journal(Path(folder) / 'offline.log')
            journal.append({'kind': 'event', 'id': 'ABC', 'sequence': 1})
            server = {'ABC'}  # committed, but local ACK was never appended
            self.assertEqual([r['id'] for r in Journal(journal.path).pending()], ['ABC'])
            self.assertIn('ABC', server)  # replay response is duplicate
            journal.append({'kind': 'ack', 'id': 'ABC'})
            self.assertEqual(Journal(journal.path).pending(), [])

    def test_fifty_events_flapping_and_reboot(self):
        with tempfile.TemporaryDirectory() as folder:
            journal = Journal(Path(folder) / 'offline.log')
            server: set[str] = set()
            for sequence in range(50):
                journal.append({'kind': 'event', 'id': f'E{sequence:02}', 'sequence': sequence})
            # Alternating batch capacity models online/offline flapping. A new
            # Journal object each round models reboot/recovery from disk.
            for capacity in (3, 0, 1, 0, 10, 4, 0, 25, 50):
                recovered = Journal(journal.path)
                for row in recovered.pending()[:capacity]:
                    server.add(row['id'])
                    journal.append({'kind': 'ack', 'id': row['id']})
            self.assertEqual(len(server), 50)
            self.assertEqual(Journal(journal.path).pending(), [])

    def test_ten_kiosks_have_jittered_retry_deadlines(self):
        base = 10_000
        deadlines = []
        for kiosk in range(10):
            rng = random.Random(kiosk)
            deadlines.append(base + base // 10 + rng.randrange(base // 10 + 1))
        self.assertTrue(all(11_000 <= value <= 12_000 for value in deadlines))
        self.assertGreater(len(set(deadlines)), 7)

if __name__ == '__main__':
    unittest.main()
