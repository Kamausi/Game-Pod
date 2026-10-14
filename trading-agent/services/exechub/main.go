package main

import (
	"context"
	_ "embed"
	"encoding/json"
	"flag"
	"log"
	"net"
	"net/http"
	"strings"
	"time"

	"github.com/gorilla/websocket"
	"google.golang.org/grpc"

	pb "tradebot/exechub/pb"
)

//go:embed dashboard.html
var dashboard []byte

type grpcServer struct {
	pb.UnimplementedExecutionHubServer
	h *Hub
}

var sides = map[pb.Side]string{pb.Side_BUY: "buy", pb.Side_SELL: "sell"}
var types = map[pb.OrderType]string{pb.OrderType_MARKET: "market", pb.OrderType_LIMIT: "limit"}
var classes = map[pb.AssetClass]string{pb.AssetClass_CRYPTO: "crypto", pb.AssetClass_STOCK: "stock", pb.AssetClass_FOREX: "forex"}
var statuses = map[string]pb.Status{"FILLED": pb.Status_FILLED, "ACCEPTED": pb.Status_ACCEPTED, "REJECTED": pb.Status_REJECTED, "DUPLICATE": pb.Status_DUPLICATE}

func (s *grpcServer) Submit(_ context.Context, in *pb.OrderIntent) (*pb.OrderResult, error) {
	r := s.h.Submit(Intent{ClientOrderID: in.ClientOrderId, AssetID: in.AssetId, AssetClass: classes[in.AssetClass], Tier: in.Tier,
		Side: sides[in.Side], OrderType: types[in.OrderType], Qty: in.Quantity, LimitPrice: in.LimitPrice, RefBid: in.RefBid,
		RefAsk: in.RefAsk, QuoteTsMs: in.QuoteTsMs, Equity: in.Equity, ReduceOnly: in.ReduceOnly, StrategyID: in.StrategyId})
	return &pb.OrderResult{ClientOrderId: r.ClientOrderID, Status: statuses[r.Status], FilledQty: r.FilledQty, AvgPrice: r.AvgPrice,
		Fee: r.Fee, Reasons: r.Reasons, HubLatencyUs: r.LatencyUs}, nil
}

func (s *grpcServer) StreamEvents(_ *pb.StreamRequest, st pb.ExecutionHub_StreamEventsServer) error {
	ch, cancel := s.h.Subscribe()
	defer cancel()
	for {
		select {
		case <-st.Context().Done():
			return nil
		case e := <-ch:
			if err := st.Send(&pb.Event{TsMs: e.TsMs, Kind: e.Kind, ClientOrderId: e.ClientOrderID, AssetId: e.AssetID, AssetClass: e.AssetClass,
				Side: e.Side, Qty: e.Qty, Price: e.Price, Status: e.Status, Detail: e.Detail}); err != nil {
				return err
			}
		}
	}
}

func (s *grpcServer) state() *pb.HubState {
	st := s.h.State()
	return &pb.HubState{Halted: st.Halted, HaltReason: st.HaltReason, Orders: st.Orders, Rejects: st.Rejects,
		LatencyP50Us: st.P50Us, LatencyP99Us: st.P99Us, Positions: st.Positions}
}

func (s *grpcServer) Halt(_ context.Context, r *pb.HaltRequest) (*pb.HubState, error) {
	s.h.Halt(r.Reason)
	return s.state(), nil
}

func (s *grpcServer) Resume(_ context.Context, r *pb.ResumeRequest) (*pb.HubState, error) {
	s.h.Resume(r.Operator)
	return s.state(), nil
}

func (s *grpcServer) State(context.Context, *pb.StateRequest) (*pb.HubState, error) { return s.state(), nil }

// Browsers on other sites must not be able to halt/resume or read the feed:
// only same-host (localhost dashboard) requests are accepted.
func sameOrigin(r *http.Request) bool {
	o := r.Header.Get("Origin")
	return o == "" || strings.TrimPrefix(strings.TrimPrefix(o, "http://"), "https://") == r.Host
}

func httpMux(h *Hub) *http.ServeMux {
	up := websocket.Upgrader{CheckOrigin: sameOrigin}
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write(dashboard)
	})
	mux.HandleFunc("/stream/orders", func(w http.ResponseWriter, r *http.Request) {
		conn, err := up.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		defer conn.Close()
		ch, cancel := h.Subscribe()
		defer cancel()
		conn.WriteJSON(map[string]any{"kind": "state", "state": h.State()})
		go func() { // reader: notices the client going away
			for {
				if _, _, err := conn.ReadMessage(); err != nil {
					cancel()
					return
				}
			}
		}()
		tick := time.NewTicker(2 * time.Second)
		defer tick.Stop()
		for {
			select {
			case e, ok := <-ch:
				if !ok {
					return
				}
				if conn.WriteJSON(e) != nil {
					return
				}
			case <-tick.C:
				if conn.WriteJSON(map[string]any{"kind": "state", "state": h.State()}) != nil {
					return
				}
			}
		}
	})
	control := func(fn func(string)) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if r.Method != http.MethodPost || !sameOrigin(r) {
				http.Error(w, "POST from the dashboard only", http.StatusForbidden)
				return
			}
			var body struct {
				Reason string `json:"reason"`
			}
			json.NewDecoder(r.Body).Decode(&body)
			if body.Reason == "" {
				body.Reason = "manual"
			}
			fn(body.Reason)
			json.NewEncoder(w).Encode(h.State())
		}
	}
	mux.HandleFunc("/api/v1/control/halt", control(h.Halt))
	mux.HandleFunc("/api/v1/control/resume", control(h.Resume))
	mux.HandleFunc("/api/v1/state", func(w http.ResponseWriter, r *http.Request) { json.NewEncoder(w).Encode(h.State()) })
	return mux
}

func main() {
	grpcAddr := flag.String("grpc", "127.0.0.1:50051", "gRPC listen address")
	httpAddr := flag.String("http", "127.0.0.1:8080", "dashboard / websocket listen address")
	journal := flag.String("journal", "exechub.jsonl", "append-only journal (replayed at start)")
	flag.Parse()
	h, err := NewHub(*journal)
	if err != nil {
		log.Fatal(err)
	}
	lis, err := net.Listen("tcp", *grpcAddr)
	if err != nil {
		log.Fatal(err)
	}
	gs := grpc.NewServer()
	pb.RegisterExecutionHubServer(gs, &grpcServer{h: h})
	go func() { log.Fatal(gs.Serve(lis)) }()
	log.Printf("exechub: gRPC on %s, dashboard on http://%s (paper venue only)", *grpcAddr, *httpAddr)
	log.Fatal(http.ListenAndServe(*httpAddr, httpMux(h)))
}
