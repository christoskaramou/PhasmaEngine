# Embeddings & RAG

PhasmaAgent has a built-in RAG pipeline: index a codebase (or any text files), persist the vector
store to disk, and have the agent automatically inject the most relevant chunks into every request.

## Headers

```cpp
#include "PhasmaAgent/Agent.h"            // IEmbeddingProvider lives here
#include "PhasmaAgent/VectorStore.h"
#include "PhasmaAgent/BM25Index.h"        // optional keyword search
#include "PhasmaAgent/CodebaseIndexer.h"

// Concrete providers — include whichever you use:
#include "PhasmaAgent/GoogleEmbedding.h"
#include "PhasmaAgent/OpenAIEmbedding.h"
#include "PhasmaAgent/OllamaEmbedding.h"
```

## 1. Pick an embedding provider

```cpp
// Google — gemini-embedding-2-preview (3072 dims), free tier at aistudio.google.com
auto embedding = std::make_shared<pagent::GoogleEmbedding>(
    "AIza...", "gemini-embedding-2-preview", 3072);

// OpenAI
auto embedding = std::make_shared<pagent::OpenAIEmbedding>(
    "sk-...", "text-embedding-3-small", 1536);
// text-embedding-3-large -> 3072 dims

// Voyage (code-optimised, OpenAI-compatible)
auto embedding = std::make_shared<pagent::OpenAIEmbedding>(
    "pa-...", "voyage-code-3", 1024, "api.voyageai.com", "/v1/embeddings");

// Ollama (local, no key, no cost)
auto embedding = std::make_shared<pagent::OllamaEmbedding>("nomic-embed-text");
// or: qwen3-embedding, mxbai-embed-large, etc.
```

### Custom provider

Implement `IEmbeddingProvider` for any other service — two methods:

```cpp
class MyEmbedding : public pagent::IEmbeddingProvider {
public:
    std::vector<float> Embed(const std::string &text) override { /* ... */ }
    int Dimensions() const override { return 1536; }
};
```

## 2. Create a VectorStore and index files

```cpp
pagent::VectorStore store;
pagent::BM25Index   bm25;   // keyword index, improves recall for exact symbol names

pagent::IndexerConfig cfg;
cfg.directories          = {"/my/project/src", "/my/project/include"};
cfg.skip_directories     = {"build", ".git", "node_modules", "third_party"};
cfg.delay_ms_between_chunks = 50;   // rate-limit for API embedding providers
// cfg.extensions        = {".cpp", ".h"};  // empty = all text files
// cfg.num_threads       = 4;               // 0 = auto (cores - 4, min 2)
// cfg.max_chunk_chars   = 4000;
// cfg.chunk_overlap_lines = 3;

pagent::CodebaseIndexer indexer(
    embedding.get(), &store, &bm25,
    [](int done, int total, const std::string &file) {
        printf("[%d/%d] %s\n", done, total, file.c_str());
    },
    [](const std::string &msg) { printf("[indexer] %s\n", msg.c_str()); }
);

// Index() blocks — always call from a background thread
std::thread([&] { indexer.Index(cfg); }).detach();

// Cancel mid-way if needed
indexer.Cancel();
```

### Check what needs re-indexing (without indexing)

```cpp
auto status = indexer.CheckStatus(cfg);
printf("%d / %d files up to date\n", status.upToDate, status.totalFiles);
// status.needsIndexing, status.outdatedFiles (relative paths)
```

## 3. Persist the index

```cpp
// Save
store.SaveToBinary("myproject_nomic.bin");

// Load on next run (pass expected dims to reject mismatched files)
store.LoadFromBinary("myproject_nomic.bin", embedding->Dimensions());
store.ForEachEntry([&](const pagent::VectorEntry &e) {
    bm25.Add(e.id, e.content);   // rebuild BM25 from loaded entries
});
```

Use a filename that encodes the embedding model, e.g. `codebase_nomic-embed-text.bin`,
so switching models automatically uses a fresh index.

## 4. Wire into the Agent

```cpp
config.embedding_provider    = embedding;
config.rag_top_k             = 5;       // chunks injected per query
config.rag_min_score         = 0.1f;    // cosine similarity threshold (0–1)
config.rag_max_context_chars = 8000;    // total chars injected
config.rag_max_entry_chars   = 8000;    // max chars per single chunk

pagent::Agent agent(config);
agent.SetCodebaseStore(&store);
agent.SetCodebaseBM25(&bm25);   // enables hybrid vector + BM25 re-ranking
```

Every `agent.Send()` now automatically retrieves the most relevant chunks and prepends them to the
request. No changes to your tool handlers or message loop.

### Inject the embedding provider after construction

If the provider becomes available after the agent is created (e.g. Ollama model list loaded async):

```cpp
agent.SetEmbeddingProvider(embedding);
agent.SetCodebaseStore(&store);
agent.SetCodebaseBM25(&bm25);
```

## 5. Manual search

```cpp
auto queryVec = embedding->Embed("how does the shadow map cascade work?");

auto results = store.Search(queryVec, /*top_k=*/5, /*min_score=*/0.1f);
for (auto &r : results)
    printf("%.3f  %s\n", r.score, r.entry->content.substr(0, 100).c_str());
```

## What gets indexed

| File type | Treatment |
|---|---|
| `.cpp` / `.h` / `.hlsl` etc. | AST-chunked (C/C++) or line-chunked, text embedded |
| `.png` / `.bin` / `.spv` / `.dds` etc. | Path-only entry: `"File: path/to/asset.png"` — searchable by path, no content read |
| Files that fail embedding | Tombstone entry (zero vector, invisible to search, marked up-to-date so they are not retried every run) |

The binary / path-only extension list is `IndexerConfig::skip_extensions`. Add or remove entries
to control which files get full content indexing vs. path-only indexing.

## Repo map (optional)

A compact codebase overview (~1K tokens) prepended to the system prompt, giving the model a structural
bird's-eye view before RAG chunks are injected:

```cpp
#include "PhasmaAgent/RepoMap.h"

pagent::RepoMap::Config rmCfg;
rmCfg.directories = cfg.directories;
std::string map = pagent::RepoMap::Generate(rmCfg);

agent.SetRepoMap(map);
// or set it in config before construction:
config.repo_map = map;
```

## Include graph context expansion (optional)

For C/C++ codebases, automatically expand RAG results to include the headers each matched file depends on:

```cpp
#include "PhasmaAgent/IncludeGraph.h"

auto graph = std::make_shared<pagent::IncludeGraph>();
graph->Build(cfg.directories);

agent.SetIncludeGraph(graph.get());
```
