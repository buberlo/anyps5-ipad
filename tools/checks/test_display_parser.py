#!/usr/bin/env python3
"""Validate summaries using disjoint frame distributions, never averaged percentiles."""
import importlib.util
from pathlib import Path
import json
import unittest
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("display", root / "tools/perf/summarize_display.py")
display = importlib.util.module_from_spec(spec);spec.loader.exec_module(display)


def row(t, count, histogram, epoch=1, actual=None):
    return {"schema":1,"source":display.SOURCE,"monotonic_ns":t,"actual_ns": t if actual is None else actual,
            "displayed_total":count,"swapchain_displayed_total":count,"samples":256,"epoch":epoch,"swapchain_id":1,
            "width":1280,"height":720,"refresh_ns":16666667,"missed_vblanks":0,"active":1,
            "histogram_step_ns":100000,"interval_histogram":histogram[:]}


class TestDisplay(unittest.TestCase):
    def test_exact_display_rate_and_histogram(self):
        h = [0] * 257;h[166] = 60
        first=row(1_000_000_000,61,h)
        h[166] += 600
        last=row(11_000_000_000,661,h)
        report=display.summarize([first,last],0,10)
        segment=report["segments"][0]
        self.assertEqual(segment["displayed_fps"],60)
        self.assertTrue(segment["minimum_duration_met"])
        self.assertEqual(segment["p99"],{"upper_ms":16.7,"over_ms":None})
    def test_no_synthetic_or_submission_rate(self):
        self.assertEqual(display.read_rows('[submit] {"fps":60}'),[])
        self.assertEqual(display.summarize([],0)["status"],"insufficient_display_timing")
    def test_counter_mismatch_rejected(self):
        h=[0]*257;h[166]=10;a=row(1,11,h);h[166]+=10;b=row(2,99,h)
        with self.assertRaisesRegex(ValueError,"does not match"):display.summarize([a,b],0)
    def test_regression_rejected(self):
        h=[0]*257;a=row(2,2,h);b=row(1,1,h)
        with self.assertRaisesRegex(ValueError,"regression"):display.summarize([a,b],0)
    def test_foreground_epochs_not_combined(self):
        h=[0]*257;h[166]=1;a=row(1,2,h);h[166]+=1;b=row(10,3,h,epoch=2)
        self.assertEqual(display.summarize([a,b],0)["segments"],[])
    def test_overflow_not_claimed_as_precise(self):
        h=[0]*257;h[256]=1
        self.assertEqual(display.percentile(h,.99),{"upper_ms":None,"over_ms":25.6})
    def test_malformed_and_wrong_source_rejected(self):
        with self.assertRaisesRegex(ValueError,"Malformed"):display.read_rows('[anyps5-display] {')
        h=[0]*257;a=row(1,1,h);a["source"]="accepted_submit"
        with self.assertRaisesRegex(ValueError,"Unsupported"):display.read_rows('[anyps5-display] '+json.dumps(a))
    def test_native_json_validated(self):
        h=[0]*257;a=row(1,1,h)
        self.assertEqual(display.read_rows('prefix [anyps5-display] '+json.dumps(a)),[a])
        a["interval_histogram"]=[0]
        with self.assertRaisesRegex(ValueError,"histogram"):display.read_rows('[anyps5-display] '+json.dumps(a))
    def test_warmup_excludes_early_frames(self):
        h=[0]*257;a=row(0,1,h,actual=1);h[256]+=2;b=row(2_000_000_000,3,h)
        h[166]+=60;c=row(3_000_000_000,63,h)
        result=display.summarize([a,b,c],2,1)["segments"][0]
        self.assertEqual(result["displayed_fps"],60)
        self.assertEqual(result["p99"]["upper_ms"],16.7)

if __name__ == '__main__':unittest.main()
