


```c
// CLOCK_REALTIME
void ktime_get_real_ts64(struct timespec64 *ts)
{
	struct timekeeper *tk = &tk_core.timekeeper;
	unsigned int seq;
	u64 nsecs;

	WARN_ON(timekeeping_suspended);

	do {
		seq = read_seqcount_begin(&tk_core.seq);

		ts->tv_sec = tk->xtime_sec;
		nsecs = timekeeping_get_ns(&tk->tkr_mono) {
			u64 delta;

			delta = timekeeping_get_delta(&tk->tkr_mono);
			return timekeeping_delta_to_ns(&tk->tkr_mono, delta);
		}

	} while (read_seqcount_retry(&tk_core.seq, seq));

	ts->tv_nsec = 0;
	timespec64_add_ns(ts, nsecs);
}

// CLOCK_REALTIME_COARSE
void ktime_get_coarse_real_ts64(struct timespec64 *ts)
{
	struct timekeeper *tk = &tk_core.timekeeper;
	unsigned int seq;

	do {
		seq = read_seqcount_begin(&tk_core.seq);

		*ts = tk_xtime(tk) {
			struct timespec64 ts;

			ts.tv_sec = tk->xtime_sec;
			ts.tv_nsec = (long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);
			return ts;
		}
	} while (read_seqcount_retry(&tk_core.seq, seq));
}
```
