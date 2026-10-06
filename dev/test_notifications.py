"""Exercise the built PaySplit component with mocked LNbits host services.

Run after dev/build.sh using the LNbits Python environment:
    python -m unittest discover -s dev -p 'test_*.py'
"""

import json
import unittest
from pathlib import Path

from wasmtime import Config, Engine, Store, WasiConfig, component

ROOT = Path(__file__).resolve().parents[1]


def record(**fields):
    value = component.Record()
    for name, field in fields.items():
        setattr(value, name.replace("_", "-"), field)
    return value


class PaymentNotificationsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        config = Config()
        config.wasm_component_model = True
        cls.engine = Engine(config)
        cls.module = component.Component.from_file(
            cls.engine, str(ROOT / "wasm/module.wasm")
        )

    def setUp(self):
        self.source = {"id": "wallet-1", "enabled": True, "max_amount": 0}
        self.targets = [
            {
                "id": "alice",
                "alias": "Alice",
                "lnurl": "alice@example.com",
                "percent": 50,
            },
            {"id": "bob", "alias": "", "lnurl": "bob@example.com", "percent": 30},
            {
                "id": "charlie",
                "alias": "Charlie",
                "lnurl": "charlie@example.com",
                "percent": 20,
            },
        ]
        self.outcomes = ["success", "pending", "fail"]
        self.calls = []
        self.notifications = []
        self.splits = []
        self.payments = []
        self.queued = True

    def storage_get(self, store, request):
        self.assertEqual(request.table, "sources")
        self.assertEqual(request.id, "wallet-1")
        return record(data_json=json.dumps(self.source) if self.source else None)

    def storage_get_paginated(self, store, request):
        self.assertEqual(request.table, "targets")
        return record(
            rows_json=json.dumps(self.targets, ensure_ascii=False),
            total=len(self.targets),
        )

    def storage_set(self, store, request):
        self.assertEqual(request.table, "splits")
        self.calls.append("save")
        self.splits.append(json.loads(getattr(request, "data-json")))
        return record(ok=True)

    def pay_lnurl(self, store, request):
        self.assertEqual(getattr(request, "wallet-id"), "wallet-1")
        outcome = self.outcomes[len(self.payments)]
        self.calls.append("pay")
        self.payments.append((request.lnurl, request.amount))
        return record(
            ok=outcome != "fail",
            error="Payment failed" if outcome == "fail" else None,
            checking_id="checking-id",
            payment_hash="payment-hash",
            status=outcome,
            amount_msat=int(request.amount * 1000),
            fee_msat=0,
            pending=outcome == "pending",
            success=outcome == "success",
        )

    def notify(self, store, request):
        self.assertEqual(set(vars(request)), {"type", "message"})
        self.assertTrue(request.message.strip())
        self.assertLessEqual(len(request.message), 4096)
        self.calls.append(request.type)
        self.notifications.append((request.type, request.message))
        return record(queued=self.queued)

    def invoke(self, **event_fields):
        store = Store(self.engine)
        store.set_wasi(WasiConfig())
        linker = component.Linker(self.engine)
        linker.add_wasip2()
        with linker.root() as root:
            with root.add_instance("lnbits:extension/host") as host:
                for name, handler in {
                    "storage-get": self.storage_get,
                    "storage-set": self.storage_set,
                    "storage-get-paginated": self.storage_get_paginated,
                    "storage-delete": lambda store, request: record(ok=True),
                    "pay-lnurl": self.pay_lnurl,
                    "list-user-wallets": lambda store: record(wallets=[]),
                    "random-id": lambda store, request: record(
                        id=f"split-{len(self.splits)}"
                    ),
                    "now": lambda store: record(timestamp=1000),
                    "log": lambda store, request: record(ok=True),
                    "notifications-send-user-notification": self.notify,
                }.items():
                    host.add_func(name, handler)
        instance = linker.instantiate(store, self.module)
        function = instance.get_func(store, "record-payment")
        event = {
            "walletId": "wallet-1",
            "paymentHash": "incoming-hash",
            "amountMsat": 1000000,
        }
        event.update(event_fields)
        result = function(store, json.dumps(event))
        function.post_return(store)
        return json.loads(result)

    def test_summary_sent_to_both_channels_after_all_splits(self):
        result = self.invoke()
        message = (
            "Received amount: 1000 sats\n"
            "Split 1: Alice 500 sats. success\n"
            "Split 2: bob@example.com 300 sats. pending\n"
            "Split 3: Charlie 200 sats. fail"
        )
        self.assertEqual(
            self.notifications, [("nostr", message), ("telegram", message)]
        )
        self.assertEqual(self.calls, ["pay", "save"] * 3 + ["nostr", "telegram"])
        self.assertEqual(
            self.payments,
            [
                ("alice@example.com", 500),
                ("bob@example.com", 300),
                ("charlie@example.com", 200),
            ],
        )
        self.assertEqual(
            [split["status"] for split in self.splits], ["success", "pending", "failed"]
        )
        self.assertEqual(
            result, {"ok": True, "data": {"sent": 1, "failed": 1, "pending": 1}}
        )

    def test_skipped_events_do_not_notify_or_pay(self):
        for event in (
            {"amountMsat": 0},
            {"splitted": True},
            {"background_payment": True},
            {"walletId": ""},
        ):
            with self.subTest(event=event):
                self.assertIn("skipped", self.invoke(**event)["data"])
        self.assertEqual(self.calls, [])

    def test_disabled_missing_or_over_limit_sources_do_not_notify(self):
        for source in (None, {"enabled": False}, {"enabled": True, "max_amount": 500}):
            with self.subTest(source=source):
                self.source = source
                self.assertIn("skipped", self.invoke()["data"])
        self.assertEqual(self.calls, [])

    def test_rounding_and_zero_amount_targets(self):
        self.targets[0]["percent"] = 0.01
        self.targets[1]["percent"] = 50
        self.targets[2]["percent"] = 49.99
        self.outcomes = ["success", "pending"]
        self.invoke(amountMsat=3500)
        message = (
            "Received amount: 3 sats\n"
            "Split 1: bob@example.com 1 sats. success\n"
            "Split 2: Charlie 1 sats. pending"
        )
        self.assertEqual(
            self.notifications, [("nostr", message), ("telegram", message)]
        )

    def test_unicode_alias(self):
        self.targets[0]["alias"] = "Ștefan ⚡"
        self.invoke()
        self.assertIn("Split 1: Ștefan ⚡ 500 sats. success", self.notifications[0][1])

    def test_telegram_escapes_aliases_and_lnurls(self):
        self.targets[0]["alias"] = "Alice_[team]*`"
        self.targets[1]["lnurl"] = "bob_smith@example.com"
        self.invoke()
        nostr = self.notifications[0][1]
        telegram = self.notifications[1][1]
        self.assertIn("Alice_[team]*`", nostr)
        self.assertIn(r"Alice\_\[team]\*\`", telegram)
        self.assertIn("bob_smith@example.com", nostr)
        self.assertIn(r"bob\_smith@example.com", telegram)

    def test_long_summaries_preserve_every_split(self):
        self.targets = [
            {
                "id": str(i),
                "alias": f"Target {i} " + "⚡" * 30,
                "lnurl": f"{i}@example.com",
                "percent": 1,
            }
            for i in range(100)
        ]
        self.outcomes = ["pending"] * len(self.targets)
        self.invoke()
        expected = "Received amount: 1000 sats" + "".join(
            f"\nSplit {i + 1}: {target['alias']} 10 sats. pending"
            for i, target in enumerate(self.targets)
        )
        for channel in ("nostr", "telegram"):
            messages = [
                message for kind, message in self.notifications if kind == channel
            ]
            self.assertGreater(len(messages), 1)
            self.assertEqual("".join(messages), expected)

    def test_single_long_unicode_alias_is_not_truncated(self):
        self.targets[0]["alias"] = "⚡" * 2000 + "_" * 3000
        self.invoke()
        nostr = "".join(
            message for kind, message in self.notifications if kind == "nostr"
        )
        telegram = "".join(
            message for kind, message in self.notifications if kind == "telegram"
        )
        self.assertIn(self.targets[0]["alias"], nostr)
        self.assertEqual(telegram, nostr.replace("_", r"\_"))
        for kind, message in self.notifications:
            if kind == "telegram":
                trailing = len(message) - len(message.rstrip("\\"))
                self.assertEqual(trailing % 2, 0)

    def test_unqueued_notification_does_not_skip_other_channel(self):
        self.queued = False
        self.assertTrue(self.invoke()["ok"])
        self.assertEqual(
            [channel for channel, _ in self.notifications], ["nostr", "telegram"]
        )

    def test_configuration_requests_notification_permission(self):
        config = json.loads((ROOT / "config.json").read_text())
        self.assertIn(
            "notifications.send_user_notification",
            [p["id"] for p in config["permissions"]],
        )


if __name__ == "__main__":
    unittest.main()
