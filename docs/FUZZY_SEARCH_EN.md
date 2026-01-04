---
cssclass: wide-content
---

# Fuzzy Search Implementation

> **Display Note:** For better readability in Obsidian, disable the content width limit in `Settings → Appearance → Maximum content width` or use "Reading mode" (`Ctrl+E`).

## Overview

The fuzzy search (approximate search) system in imSid Player allows finding SID tracks even with typos or variations in names. This functionality is **fully custom** and does not use any external library.

**Important note**: Glaze is only used for JSON serialization/deserialization. It does **not** provide fuzzy search functionality. The `fuzzing` folder in the Glaze repository refers to "fuzz testing" (robustness testing), not approximate search.

## Architecture

### Related Files

- `src/DatabaseManager.cpp`: Main implementation
- `include/DatabaseManager.h`: Public interface
- `src/UIManager.cpp`: User interface and result display

### Main Function: `DatabaseManager::search()`

```cpp
std::vector<const SidMetadata*> DatabaseManager::search(const std::string& query) const
```

This function performs a two-pass search:

1. **Exact search** (fast): Searches the query as a substring in title, author, and filename
2. **Fuzzy search** (if needed): Uses the `fuzzyMatchFast()` algorithm for approximate matches

## Fuzzy Matching Algorithm

### Function: `fuzzyMatchFast()`

```cpp
double DatabaseManager::fuzzyMatchFast(const std::string& str, const std::string& query)
```

#### How It Works

1. **Exact substring check**
   - If `query` is a substring of `str` → score = 1.0 (perfect match)

2. **Subsequence Matching** (if no exact match)
   - Checks if all characters of `query` are present in `str` **in order**
   - Characters don't need to be consecutive
   - Example: `query = "david"` in `str = "David Major - Cova"` → ✅ Match

3. **Score calculation**
   - **60%** based on the proportion of characters found (`matchRatio`)
   - **40%** based on the length of the longest consecutive sequence (`consecutiveRatio`)
   - Final score: `(matchRatio * 0.6 + consecutiveRatio * 0.4)`

#### Complexity

- **Time**: O(n) where n = length of the string to search
- **Space**: O(1)
- **Advantage**: Much faster than Levenshtein algorithm (O(n×m))

#### Example

```cpp
query = "david"
str = "David Major - Cova"

// Step 1: Exact substring?
"David Major - Cova".find("david") → Not found (case-sensitive after tolower)

// Step 2: Subsequence matching
d → found at index 0
a → found at index 1
v → found at index 2
i → found at index 3
d → found at index 4

// All characters found in order → High score (close to 1.0)
```

## Multi-Word Search

### Query Parsing

The query is split into words separated by spaces:

```cpp
std::vector<std::string> queryWords;
std::istringstream iss(queryLower);
std::string word;
while (iss >> word) {
    if (word.length() >= 2) { // Ignore words that are too short
        queryWords.push_back(word);
    }
}
```

### Search Strategy

If the query contains **multiple words** (`queryWords.size() > 1`):

1. **For each word in the query**:
   - Exact search in title, author, filename
   - If not found, fuzzy search with threshold 0.4
   - Priority: Title > Author > Filename

2. **Final score calculation**:
   - If **all words** are found: `score = (totalFuzzyScore / queryWords.size()) * 5.0`
   - If **partial match**: `score = (wordsFound / queryWords.size()) * (avgScore) * 3.0` (penalty)

3. **Example**:
   ```
   Query: "David Major cova"
   → Words: ["david", "major", "cova"]
   
   For each word, search in:
   - Title: "Cova" (exact match for "cova")
   - Author: "David Major" (exact match for "david" and "major")
   
   → All words found → High score
   ```

If the query contains **a single word**:

- Simple fuzzy search with threshold 0.5
- Priority: Title (×5.0) > Author (×4.0) > Filename (×2.0)

## Limits and Performance

### Result Limit

- **Maximum 25 results** returned
- Results are sorted by descending score

### Optimizations

1. **Early exit**: If 25+ exact results are found, fuzzy search is skipped
2. **Avoid duplicates**: Uses an `std::unordered_set` to avoid re-testing metadata already found in exact match
3. **Adaptive thresholds**:
   - Exact match: no threshold (always included)
   - Fuzzy single word: threshold 0.5
   - Fuzzy multi-word: threshold 0.4 per word

### Logging

Slow searches (> 50ms) are logged:

```
[SEARCH] query='David Major cova': 170 ms (DB: 59324, exact: 0, fuzzy: yes, results: 3)
```

## Usage in the Interface

### Search Field

- **Location**: "Playlist" panel → "Search:" field
- **Behavior**: Real-time search on each keystroke
- **Display**: Scrollable list under the field (max 25 results)

### Keyboard Navigation

- **Down arrow** from the field → Selects the first result
- **Up/Down arrows** in the list → Navigate between results
- **Enter** → Selects the result and loads the track

### Highlighting

- The selected result is highlighted in blue
- Automatic scroll to the selected element

## Usage Examples

### Simple Search

```
Query: "david"
→ Finds all tracks with "david" in title, author or filename
→ Tolerates variations: "David", "DAVID", "dAvId"
```

### Multi-Word Search

```
Query: "David Major cova"
→ Finds tracks where:
  - "david" is present (title, author or filename)
  - "major" is present
  - "cova" is present
→ Words can be in different fields
```

### Search with Typos

```
Query: "davd" (missing 'i')
→ Still finds "David" thanks to fuzzy matching
→ Reduced score because not all characters are consecutive
```

## Future Improvements

1. **Indexing**: Use an inverted index to speed up searches
2. **Weighting**: Adjust weights based on field (title more important than filename)
3. **History**: Save frequent searches
4. **Suggestions**: Propose automatic corrections for queries with no results
5. **Phonetic search**: Support for phonetic variations (e.g., "ph" vs "f")

## References

- **Algorithm inspired by**: Subsequence matching (simplified variant)
- **More precise alternative**: Levenshtein distance (but O(n×m) so slower)
- **Library used**: None (custom implementation)

