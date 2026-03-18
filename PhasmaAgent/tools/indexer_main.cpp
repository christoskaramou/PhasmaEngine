#include "PhasmaAgent/CodebaseIndexer.h"
#include "PhasmaAgent/GoogleEmbedding.h"
#include "PhasmaAgent/OpenAIEmbedding.h"
#include "PhasmaAgent/OllamaEmbedding.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>

static void PrintUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --provider <google|openai|ollama>  Embedding provider (default: google)\n"
              << "  --model <name>                     Embedding model name\n"
              << "  --api-key <key>                    API key (or set PAGENT_GEMINI_API_KEY / PAGENT_OPENAI_API_KEY)\n"
              << "  --dirs <dir1;dir2;...>             Semicolon-separated directories to scan\n"
              << "  --output <path>                    Output vectors JSON file\n"
              << "  --chunk-size <chars>               Max chunk size in chars (default: 4000)\n"
              << "  --delay <ms>                       Delay between API calls in ms (default: 50)\n";
}

static std::vector<std::string> SplitString(const std::string &s, char delim)
{
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim))
        if (!item.empty())
            result.push_back(item);
    return result;
}

int main(int argc, char *argv[])
{
    std::string provider = "google";
    std::string model;
    std::string apiKey;
    std::string dirsStr;
    std::string output;
    int chunkSize = 4000;
    int delay = 50;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--provider" && i + 1 < argc)
            provider = argv[++i];
        else if (arg == "--model" && i + 1 < argc)
            model = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc)
            apiKey = argv[++i];
        else if (arg == "--dirs" && i + 1 < argc)
            dirsStr = argv[++i];
        else if (arg == "--output" && i + 1 < argc)
            output = argv[++i];
        else if (arg == "--chunk-size" && i + 1 < argc)
            chunkSize = std::stoi(argv[++i]);
        else if (arg == "--delay" && i + 1 < argc)
            delay = std::stoi(argv[++i]);
        else if (arg == "--help" || arg == "-h")
        {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    if (dirsStr.empty())
    {
        std::cerr << "Error: --dirs is required\n";
        PrintUsage(argv[0]);
        return 1;
    }
    if (output.empty())
    {
        std::cerr << "Error: --output is required\n";
        PrintUsage(argv[0]);
        return 1;
    }

    // Create embedding provider
    std::shared_ptr<pagent::IEmbeddingProvider> embedding;

    if (provider == "google")
    {
        if (apiKey.empty())
        {
            const char *env = std::getenv("PAGENT_GEMINI_API_KEY");
            if (env)
                apiKey = env;
        }
        if (apiKey.empty())
        {
            std::cerr << "Error: Google API key required (--api-key or PAGENT_GEMINI_API_KEY)\n";
            return 1;
        }
        if (model.empty())
            model = "gemini-embedding-2-preview";
        embedding = std::make_shared<pagent::GoogleEmbedding>(apiKey, model, 3072);
    }
    else if (provider == "openai")
    {
        if (apiKey.empty())
        {
            const char *env = std::getenv("PAGENT_OPENAI_API_KEY");
            if (env)
                apiKey = env;
        }
        if (apiKey.empty())
        {
            std::cerr << "Error: OpenAI API key required (--api-key or PAGENT_OPENAI_API_KEY)\n";
            return 1;
        }
        if (model.empty())
            model = "text-embedding-3-small";
        int dims = (model.find("large") != std::string::npos) ? 3072 : 1536;
        embedding = std::make_shared<pagent::OpenAIEmbedding>(apiKey, model, dims);
    }
    else if (provider == "ollama")
    {
        if (model.empty())
            model = "nomic-embed-text";
        embedding = std::make_shared<pagent::OllamaEmbedding>(model);
    }
    else
    {
        std::cerr << "Error: unknown provider '" << provider << "'\n";
        return 1;
    }

    // Set up vector store
    pagent::VectorStore store;
    store.LoadFromFile(output, embedding->Dimensions());

    // Configure indexer
    pagent::IndexerConfig config;
    config.directories = SplitString(dirsStr, ';');
    config.max_chunk_chars = chunkSize;
    config.delay_ms_between_chunks = delay;

    pagent::CodebaseIndexer indexer(
        embedding.get(), &store, nullptr,
        [](int done, int total, const std::string &file)
        {
            std::cout << "[" << done << "/" << total << "] " << file << "\n";
        },
        [](const std::string &msg)
        {
            std::cout << msg << "\n";
        });

    int chunks = indexer.Index(config);
    store.SaveToFile(output);

    std::cout << "Done: " << chunks << " chunks indexed -> " << output << "\n";
    return 0;
}
