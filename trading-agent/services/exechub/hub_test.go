package main

import (
	"path/filepath"
	"testing"
	"time"
)

func intent(id string) Intent {
	return Intent{ClientOrderID: id, AssetID: "BTC_USD", AssetClass: "crypto", Tier: "crypto_major", Side: "buy",
		OrderType: "market", Qty: 0.01, RefBid: 83000, RefAsk: 83001, QuoteTsMs: time.Now().UnixMilli(), Equity: 10000}
}

func TestFillAndDuplicate(t *testing.T) {
	h, _ := NewHub("")
	r := h.Submit(intent("a"))
	if r.Status != "FILLED" || r.FilledQty != 0.01 {
		t.Fatalf("want fill, got %+v", r)
	}
	if d := h.Submit(intent("a")); d.Status != "DUPLICATE" || h.State().Positions["BTC_USD"] != 0.01 {
		t.Fatalf("duplicate was not suppressed: %+v %+v", d, h.State())
	}
}

func TestRiskRejections(t *testing.T) {
	h, _ := NewHub("")
	big := intent("big")
	big.Qty = 1 // $83k on $10k equity
	stale := intent("stale")
	stale.QuoteTsMs = time.Now().Add(-time.Minute).UnixMilli()
	meme := intent("meme")
	meme.Tier = "crypto_high_vol"
	wide := intent("wide")
	wide.RefAsk = 84000
	for _, in := range []Intent{big, stale, meme, wide} {
		if r := h.Submit(in); r.Status != "REJECTED" {
			t.Errorf("%s should be rejected, got %+v", in.ClientOrderID, r)
		}
	}
}

func TestHaltAllowsOnlyExits(t *testing.T) {
	h, _ := NewHub("")
	h.Submit(intent("open"))
	h.Halt("test")
	if r := h.Submit(intent("new")); r.Status != "REJECTED" {
		t.Fatalf("new risk accepted while halted: %+v", r)
	}
	exit := intent("exit")
	exit.Side, exit.ReduceOnly = "sell", true
	if r := h.Submit(exit); r.Status != "FILLED" {
		t.Fatalf("exit blocked while halted: %+v", r)
	}
	fake := intent("fake-exit")
	fake.ReduceOnly = true // a buy on a flat book is not reducing
	if r := h.Submit(fake); r.Status != "REJECTED" {
		t.Fatalf("fake reduce-only accepted: %+v", r)
	}
}

func TestJournalSurvivesRestart(t *testing.T) {
	p := filepath.Join(t.TempDir(), "j.jsonl")
	h, _ := NewHub(p)
	h.Submit(intent("x"))
	h.Halt("restart test")
	h2, _ := NewHub(p)
	if r := h2.Submit(intent("x")); r.Status != "DUPLICATE" {
		t.Fatalf("idempotency lost after restart: %+v", r)
	}
	if !h2.State().Halted || h2.State().Positions["BTC_USD"] != 0.01 {
		t.Fatalf("state lost after restart: %+v", h2.State())
	}
}

func TestLimitOrders(t *testing.T) {
	h, _ := NewHub("")
	in := intent("lim")
	in.Tier, in.OrderType, in.LimitPrice = "crypto_high_vol", "limit", 82990
	in.Qty = 0.005
	if r := h.Submit(in); r.Status != "ACCEPTED" {
		t.Fatalf("resting limit should be accepted, got %+v", r)
	}
}
