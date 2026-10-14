# Research prompt (run by hand in Claude, with web search on)

Use this for the slow "brain" layer. It produces a watchlist for a human to read.
It never feeds orders directly. A new instrument goes into the bot only after
it passes the launch gates in README.md on its own paper run.

---

You are a skeptical crypto research analyst. Today is {DATE}. Use live primary
sources only (project docs, token-unlock schedules, on-chain dashboards, exchange
data, filings). Cite every number with its source URL and the date you retrieved
it. If you can't source a number, write "unknown". Don't estimate silently.
Label every estimate as ESTIMATE and every forward-looking claim as SPECULATION.

1. Regime. Using BTC and ETH trend, BTC dominance, total stablecoin supply trend,
   perp funding, and open interest, classify the market as risk-on, risk-off,
   range, or crisis. Give the three data points that would change your call.

2. Candidates. Find up to 10 liquid tokens (top-100 by volume, listed on at least
   two major venues) where valuation looks disconnected from fees, revenue,
   active users, TVL, or a dated catalyst. For each one:
   - Supply: circulating vs. fully diluted, and the unlock schedule for the next
     180 days with dates and amounts.
   - Fundamentals: fees, revenue, and active users over 90 days, with the trend
     direction.
   - Value accrual: how, or whether, fee revenue reaches token holders (burns,
     buybacks, staking yield from real fees vs. emissions).
   - Holders: share held by the top 10 wallets, excluding exchanges and contracts
     where possible.
   - Catalysts: CONFIRMED (dated, from a primary source) vs. SPECULATION.
   - Bear case: write the strongest version first, before the bull case.
   - Invalidation: the specific, observable evidence that kills the thesis.
   - Priced in?: what the last 30 and 90 days of price action and funding say
     about whether the catalyst is already priced in.
   - Organic or incentivised?: what share of usage is driven by points, airdrops,
     or emissions.

3. Rank the candidates, and drop any where value doesn't accrue to the token or
   the edge is mainly incentive-driven.

4. End with "WHAT COULD I BE WRONG ABOUT?"
