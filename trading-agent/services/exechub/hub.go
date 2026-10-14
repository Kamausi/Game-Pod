// Package main is the execution hub: a small Go service that receives order
// intents from the Python strategy over gRPC, applies its OWN risk checks
// (defence in depth: the strategy's checks are not trusted), fills them on a
// paper venue, journals every event, and streams events to dashboards.
//
// It never talks to a real exchange. Paper only, by construction: there is
// no venue adapter here other than the simulated one.
package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sort"
	"sync"
	"time"
)

// Tier limits mirror tradebot/assets.py. The hub enforces them independently.
type TierRules struct {
	MaxOrderFrac float64 // single order notional / equity
	MaxSpreadPct float64
	LimitOnly    bool
}

var Tiers = map[string]TierRules{
	"crypto_major":    {0.25, 0.0025, false},
	"crypto_high_vol": {0.05, 0.005, true},
	"stock":           {0.20, 0.002, false},
	"penny_stock":     {0.02, 0.015, true},
	"forex_major":     {0.30, 0.0003, false},
}

const (
	FeeBps       = 6.0
	SlipBps      = 2.0
	MaxQuoteAge  = 5 * time.Second
	MaxOrdersDay = 500
)

type Intent struct {
	ClientOrderID string  `json:"client_order_id"`
	AssetID       string  `json:"asset_id"`
	AssetClass    string  `json:"asset_class"`
	Tier          string  `json:"tier"`
	Side          string  `json:"side"` // buy | sell
	OrderType     string  `json:"order_type"`
	Qty           float64 `json:"qty"`
	LimitPrice    float64 `json:"limit_price"`
	RefBid        float64 `json:"ref_bid"`
	RefAsk        float64 `json:"ref_ask"`
	QuoteTsMs     int64   `json:"quote_ts_ms"`
	Equity        float64 `json:"equity"`
	ReduceOnly    bool    `json:"reduce_only"`
	StrategyID    string  `json:"strategy_id"`
}

type Result struct {
	ClientOrderID string   `json:"client_order_id"`
	Status        string   `json:"status"` // FILLED | ACCEPTED | REJECTED | DUPLICATE
	FilledQty     float64  `json:"filled_qty"`
	AvgPrice      float64  `json:"avg_price"`
	Fee           float64  `json:"fee"`
	Reasons       []string `json:"reasons"`
	LatencyUs     int64    `json:"hub_latency_us"`
}

type Event struct {
	TsMs          int64   `json:"ts_ms"`
	Kind          string  `json:"kind"`
	ClientOrderID string  `json:"client_order_id,omitempty"`
	AssetID       string  `json:"asset_id,omitempty"`
	AssetClass    string  `json:"asset_class,omitempty"`
	Side          string  `json:"side,omitempty"`
	Qty           float64 `json:"qty,omitempty"`
	Price         float64 `json:"price,omitempty"`
	Status        string  `json:"status,omitempty"`
	Detail        string  `json:"detail,omitempty"`
}

type Hub struct {
	mu         sync.Mutex
	now        func() time.Time
	results    map[string]Result // idempotency ledger
	positions  map[string]float64
	halted     bool
	haltReason string
	orders     int64
	rejects    int64
	latencies  []float64
	subs       map[chan Event]struct{}
	journal    *os.File
}

func NewHub(journalPath string) (*Hub, error) {
	h := &Hub{now: time.Now, results: map[string]Result{}, positions: map[string]float64{}, subs: map[chan Event]struct{}{}}
	if journalPath != "" {
		// Replay the journal so idempotency and positions survive a restart.
		if b, err := os.ReadFile(journalPath); err == nil {
			for _, line := range splitLines(b) {
				var rec struct {
					Result *Result `json:"result"`
					Intent *Intent `json:"intent"`
					Event  *Event  `json:"event"`
				}
				if json.Unmarshal(line, &rec) != nil {
					continue
				}
				if rec.Result != nil && rec.Intent != nil {
					h.results[rec.Result.ClientOrderID] = *rec.Result
					if rec.Result.Status == "FILLED" {
						h.positions[rec.Intent.AssetID] += signed(rec.Intent.Side, rec.Result.FilledQty)
					}
				}
				if rec.Event != nil && (rec.Event.Kind == "halt" || rec.Event.Kind == "resume") {
					h.halted = rec.Event.Kind == "halt"
					h.haltReason = rec.Event.Detail
				}
			}
		}
		f, err := os.OpenFile(journalPath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
		if err != nil {
			return nil, err
		}
		h.journal = f
	}
	return h, nil
}

func splitLines(b []byte) [][]byte {
	var out [][]byte
	start := 0
	for i, c := range b {
		if c == '\n' {
			if i > start {
				out = append(out, b[start:i])
			}
			start = i + 1
		}
	}
	if start < len(b) {
		out = append(out, b[start:])
	}
	return out
}

func signed(side string, q float64) float64 {
	if side == "sell" {
		return -q
	}
	return q
}

func (h *Hub) write(v any) {
	if h.journal == nil {
		return
	}
	b, _ := json.Marshal(v)
	h.journal.Write(append(b, '\n'))
}

func (h *Hub) emit(e Event) {
	h.write(map[string]any{"event": e})
	for ch := range h.subs {
		select {
		case ch <- e:
		default: // slow subscriber: drop rather than block trading
		}
	}
}

func (h *Hub) Subscribe() (chan Event, func()) {
	ch := make(chan Event, 256)
	h.mu.Lock()
	h.subs[ch] = struct{}{}
	h.mu.Unlock()
	return ch, func() {
		h.mu.Lock()
		delete(h.subs, ch)
		h.mu.Unlock()
	}
}

// Check returns the reasons an intent must be refused (empty = allowed).
func (h *Hub) Check(in Intent) []string {
	var why []string
	rules, ok := Tiers[in.Tier]
	if !ok {
		return []string{"unknown tier " + in.Tier}
	}
	if in.Qty <= 0 || in.RefBid <= 0 || in.RefAsk < in.RefBid || in.Equity <= 0 {
		return []string{"invalid quantity, quote or equity"}
	}
	if in.Side != "buy" && in.Side != "sell" {
		return []string{"side must be buy or sell"}
	}
	pos := h.positions[in.AssetID]
	reducing := in.ReduceOnly && math.Abs(pos+signed(in.Side, in.Qty)) < math.Abs(pos)+1e-12 && pos*signed(in.Side, in.Qty) < 0
	if in.ReduceOnly && !reducing {
		why = append(why, "marked reduce-only but would not reduce the position")
	}
	if reducing {
		return why // exits are always allowed
	}
	if h.halted {
		why = append(why, "hub halted: "+h.haltReason)
	}
	age := h.now().Sub(time.UnixMilli(in.QuoteTsMs))
	if age > MaxQuoteAge || age < -MaxQuoteAge {
		why = append(why, fmt.Sprintf("stale quote (%s old)", age.Round(time.Millisecond)))
	}
	mid := (in.RefBid + in.RefAsk) / 2
	if spread := (in.RefAsk - in.RefBid) / mid; spread > rules.MaxSpreadPct {
		why = append(why, fmt.Sprintf("spread %.3f%% over %.3f%%", spread*100, rules.MaxSpreadPct*100))
	}
	if frac := in.Qty * mid / in.Equity; frac > rules.MaxOrderFrac+1e-9 {
		why = append(why, fmt.Sprintf("order is %.1f%% of equity, tier cap %.0f%%", frac*100, rules.MaxOrderFrac*100))
	}
	if rules.LimitOnly && in.OrderType != "limit" {
		why = append(why, in.Tier+" accepts limit orders only")
	}
	if in.OrderType == "limit" && in.LimitPrice <= 0 {
		why = append(why, "limit order without a limit price")
	}
	if h.orders >= MaxOrdersDay {
		why = append(why, "daily order count limit")
	}
	return why
}

// Submit is the only order path. Idempotent on ClientOrderID.
func (h *Hub) Submit(in Intent) Result {
	t0 := time.Now()
	h.mu.Lock()
	defer h.mu.Unlock()
	if prev, ok := h.results[in.ClientOrderID]; ok {
		prev.Status = "DUPLICATE"
		return prev
	}
	res := Result{ClientOrderID: in.ClientOrderID}
	if in.ClientOrderID == "" {
		res.Status, res.Reasons = "REJECTED", []string{"missing client_order_id"}
		return res
	}
	if why := h.Check(in); len(why) > 0 {
		res.Status, res.Reasons = "REJECTED", why
		h.rejects++
	} else if in.OrderType == "limit" && !marketable(in) {
		res.Status = "ACCEPTED" // a resting limit; this paper venue does not work it further
		h.orders++
	} else {
		px := in.RefAsk * (1 + SlipBps/1e4)
		if in.Side == "sell" {
			px = in.RefBid * (1 - SlipBps/1e4)
		}
		if in.OrderType == "limit" {
			px = in.LimitPrice
		}
		res.Status, res.FilledQty, res.AvgPrice = "FILLED", in.Qty, px
		res.Fee = in.Qty * px * FeeBps / 1e4
		h.positions[in.AssetID] += signed(in.Side, in.Qty)
		h.orders++
	}
	res.LatencyUs = time.Since(t0).Microseconds()
	h.latencies = append(h.latencies, float64(res.LatencyUs))
	if len(h.latencies) > 10000 {
		h.latencies = h.latencies[5000:]
	}
	h.results[in.ClientOrderID] = res
	h.write(map[string]any{"intent": in, "result": res})
	kind := map[string]string{"FILLED": "fill", "ACCEPTED": "order", "REJECTED": "reject"}[res.Status]
	detail := ""
	if len(res.Reasons) > 0 {
		detail = res.Reasons[0]
	}
	h.emit(Event{TsMs: h.now().UnixMilli(), Kind: kind, ClientOrderID: in.ClientOrderID, AssetID: in.AssetID,
		AssetClass: in.AssetClass, Side: in.Side, Qty: in.Qty, Price: res.AvgPrice, Status: res.Status, Detail: detail})
	return res
}

func marketable(in Intent) bool {
	if in.Side == "buy" {
		return in.LimitPrice >= in.RefAsk
	}
	return in.LimitPrice <= in.RefBid
}

func (h *Hub) Halt(reason string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.halted, h.haltReason = true, reason
	h.emit(Event{TsMs: h.now().UnixMilli(), Kind: "halt", Detail: reason})
}

func (h *Hub) Resume(operator string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.halted, h.haltReason = false, ""
	h.emit(Event{TsMs: h.now().UnixMilli(), Kind: "resume", Detail: "resumed by " + operator})
}

type State struct {
	Halted     bool               `json:"halted"`
	HaltReason string             `json:"halt_reason"`
	Orders     int64              `json:"orders"`
	Rejects    int64              `json:"rejects"`
	P50Us      float64            `json:"latency_p50_us"`
	P99Us      float64            `json:"latency_p99_us"`
	Positions  map[string]float64 `json:"positions"`
}

func (h *Hub) State() State {
	h.mu.Lock()
	defer h.mu.Unlock()
	pos := map[string]float64{}
	for k, v := range h.positions {
		if v != 0 {
			pos[k] = v
		}
	}
	return State{h.halted, h.haltReason, h.orders, h.rejects, pct(h.latencies, 0.5), pct(h.latencies, 0.99), pos}
}

func pct(xs []float64, q float64) float64 {
	if len(xs) == 0 {
		return 0
	}
	s := append([]float64(nil), xs...)
	sort.Float64s(s)
	i := int(q * float64(len(s)))
	if i >= len(s) {
		i = len(s) - 1
	}
	return s[i]
}
