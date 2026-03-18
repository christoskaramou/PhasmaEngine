#include "PhasmaAgent/Agent.h"
#include <string>
#include <vector>

namespace pagent
{
    // Keywords that suggest a simple query (explanation, lookup, Q&A)
    static const std::vector<std::string> s_simpleKeywords = {
        "explain",
        "what is",
        "what does",
        "what are",
        "how does",
        "describe",
        "show me",
        "find",
        "where is",
        "list",
        "tell me",
        "define",
        "meaning of",
        "help me understand",
        "print",
        "display",
        "read",
        "get",
        "look up",
    };

    // Keywords that suggest a complex query (architecture, multi-file, design)
    static const std::vector<std::string> s_complexKeywords = {
        "architect",
        "design",
        "refactor entire",
        "rewrite",
        "restructure",
        "implement system",
        "build a",
        "create a new system",
        "add a new feature",
        "across all files",
        "every file",
        "all files",
        "multiple files",
        "multi-file",
        "codebase-wide",
        "migration",
        "overhaul",
        "redesign",
    };

    static std::string toLowerStr(const std::string &s)
    {
        std::string lower = s;
        for (auto &c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower;
    }

    static bool containsAny(const std::string &text, const std::vector<std::string> &keywords)
    {
        for (const auto &kw : keywords)
            if (text.find(kw) != std::string::npos)
                return true;
        return false;
    }

    QueryComplexity ClassifyQuery(const std::string &query)
    {
        std::string lower = toLowerStr(query);

        // Check complex first (complex keywords take priority)
        if (containsAny(lower, s_complexKeywords))
            return QueryComplexity::Complex;

        // Check simple
        if (containsAny(lower, s_simpleKeywords))
            return QueryComplexity::Simple;

        // Default to medium
        return QueryComplexity::Medium;
    }
} // namespace pagent
