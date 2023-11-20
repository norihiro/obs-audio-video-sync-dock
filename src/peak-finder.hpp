#pragma once

#include <inttypes.h>

struct peak_finder
{
	uint64_t cand_ts = 0;
	uint64_t last_ts = 0;

	float cand_score = 0.0f;
	float last_score = 0.0f;

	static float dumping(uint64_t ts_last, uint64_t ts_next)
	{
		if (ts_next <= ts_last)
			return 1.0f;
		if (ts_next - ts_last > 2000000000)
			return 0.0f;
		float f = (ts_next - ts_last) * 0.5e-9f;
		return 1.0f - f * f;
	}

	bool append(float score, uint64_t ts, uint64_t wait_ts)
	{
		if (score > cand_score * dumping(cand_ts, ts)) {
			cand_ts = ts;
			cand_score = score;
			/* When updating candidate, which is the recent peak,
			 * there might be larger score coming next. */
			return false;
		}

		if (cand_ts + wait_ts > ts)
			return false;

		if (cand_ts > last_ts && cand_score > last_score * dumping(last_ts, cand_ts)) {
			last_ts = cand_ts;
			last_score = cand_score;
			return true;
		}

		return false;
	}
};
