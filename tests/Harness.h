#pragma once
// Result recording + markdown report for the DSP test suite.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace dt
{
struct Ctx
{
    double fs = 48000.0;
    int nCh = 2;
    std::string tag() const
    {
        char b[32];
        std::snprintf (b, sizeof b, "%g k %s", fs / 1000.0, nCh == 1 ? "mono" : "stereo");
        return b;
    }
};

struct Result
{
    std::string section, check, target, unit;
    std::string config;   // sample rate / channels
    double value = 0.0;
    bool pass = true, info = false;
    double margin = 0.0;  // >= 0 passes; smaller = closer to failing
};

inline std::string fmtNum (double v)
{
    char b[48];
    if (! std::isfinite (v))
        return "nan";
    const double a = std::abs (v);
    if (a != 0.0 && (a < 0.001 || a >= 1.0e6))
        std::snprintf (b, sizeof b, "%.3g", v);
    else if (a < 1.0)
        std::snprintf (b, sizeof b, "%.4f", v);
    else if (a < 100.0)
        std::snprintf (b, sizeof b, "%.3f", v);
    else
        std::snprintf (b, sizeof b, "%.1f", v);
    return b;
}

class Recorder
{
public:
    explicit Recorder (Ctx c) : ctx (c) {}
    Ctx ctx;
    std::vector<Result> results;
    std::string section = "General";

    void less (const std::string& check, double v, double limit, const std::string& unit)
    {
        add (check, v, unit, "< " + fmtNum (limit) + " " + unit, v < limit, limit - v);
    }
    void greater (const std::string& check, double v, double limit, const std::string& unit)
    {
        add (check, v, unit, "> " + fmtNum (limit) + " " + unit, v > limit, v - limit);
    }
    void range (const std::string& check, double v, double lo, double hi, const std::string& unit)
    {
        add (check, v, unit, fmtNum (lo) + " .. " + fmtNum (hi) + " " + unit, v >= lo && v <= hi,
             std::min (v - lo, hi - v));
    }
    void equal (const std::string& check, double v, double expected, const std::string& unit)
    {
        add (check, v, unit, "= " + fmtNum (expected) + " " + unit, v == expected, -std::abs (v - expected));
    }
    void truth (const std::string& check, bool ok, const std::string& what = "yes")
    {
        add (check, ok ? 1.0 : 0.0, "", what, ok, ok ? 1.0 : -1.0);
    }
    void info (const std::string& check, double v, const std::string& unit)
    {
        Result r { section, check, "(info)", unit, ctx.tag(), v, true, true, 0.0 };
        results.push_back (r);
    }

private:
    void add (const std::string& check, double v, const std::string& unit, const std::string& target, bool pass,
              double margin)
    {
        if (! std::isfinite (v))
        {
            pass = false;
            margin = -1.0e30;
        }
        results.push_back ({ section, check, target, unit, ctx.tag(), v, pass, false, margin });
    }
};

//------------------------------------------------------------------------------
inline std::string writeReport (const std::vector<Result>& all, const std::vector<std::string>& configs,
                                const std::string& path, const std::string& header)
{
    // preserve first-seen order of (section, check)
    std::vector<std::pair<std::string, std::string>> order;
    std::map<std::pair<std::string, std::string>, std::vector<const Result*>> byCheck;
    for (auto& r : all)
    {
        auto key = std::make_pair (r.section, r.check);
        if (byCheck.find (key) == byCheck.end())
            order.push_back (key);
        byCheck[key].push_back (&r);
    }

    int nPass = 0, nFail = 0;
    for (auto& r : all)
        if (! r.info)
            (r.pass ? nPass : nFail)++;

    std::ostringstream md;
    md << "# Axell Penta EQ - DSP test report\n\n" << header << "\n\n";
    md << "**Result: " << (nFail == 0 ? "ALL PASS" : "FAILURES") << "** - " << nPass << " checks passed, " << nFail
       << " failed (every check runs once per sample rate / channel configuration).\n\n";

    md << "## Summary (worst case over all configurations)\n\n";
    md << "| Section | Check | Target | Worst measured | at | Pass |\n|---|---|---|---|---|---|\n";
    for (auto& key : order)
    {
        const auto& v = byCheck[key];
        const Result* worst = v.front();
        int p = 0, n = 0;
        for (auto* r : v)
        {
            if (r->info)
                continue;
            ++n;
            if (r->pass)
                ++p;
            if (r->margin < worst->margin || worst->info)
                worst = r;
        }
        if (v.front()->info)
        {
            double lo = v.front()->value, hi = lo;
            for (auto* r : v)
            {
                lo = std::min (lo, r->value);
                hi = std::max (hi, r->value);
            }
            md << "| " << key.first << " | " << key.second << " | (info) | " << fmtNum (lo)
               << (hi != lo ? " .. " + fmtNum (hi) : std::string()) << " " << v.front()->unit << " | all | - |\n";
            continue;
        }
        md << "| " << key.first << " | " << key.second << " | " << worst->target << " | " << fmtNum (worst->value)
           << " " << worst->unit << " | " << worst->config << " | " << (p == n ? "PASS" : "**FAIL**") << " " << p
           << "/" << n << " |\n";
    }

    md << "\n## Detail per configuration\n\n| Check |";
    for (auto& c : configs)
        md << " " << c << " |";
    md << "\n|---|";
    for (size_t i = 0; i < configs.size(); ++i)
        md << "---|";
    md << "\n";
    for (auto& key : order)
    {
        const auto& v = byCheck[key];
        md << "| " << key.first << ": " << key.second << " |";
        for (auto& c : configs)
        {
            std::string cell = " - ";
            for (auto* r : v)
                if (r->config == c)
                    cell = " " + fmtNum (r->value) + (r->info || r->pass ? "" : " **x**") + " ";
            md << cell << "|";
        }
        md << "\n";
    }

    std::ofstream f (path);
    f << md.str();
    return md.str();
}
} // namespace dt
