/**
 * @file ram_index.h
 */

#ifndef _RAM_INDEX_
#define _RAM_INDEX_

#include <string>

#include "document.h"
#include "tokenizer.h"
#include "index.h"

using std::string;

/**
 *
 */
class RAMIndex : public Index
{
    public:

        /**
         * Creates an Index located in memory.
         * @param indexFiles - files to index
         * @param tokenizer - how to tokenize the indexed files 
         */
        RAMIndex(const vector<string> & indexFiles,
                 const Tokenizer* tokenizer);

        /**
         * Scores a document given a query.
         * @param document - the doc to score
         * @param query - the query to score against
         * @return the real score value 
         */
        double scoreDocument(const Document & document,
                             const Document & query) const;

        /**
         * @return the average document length of the collection
         */
        size_t getAvgDocLength() const;

        /**
         * Searches the index using the scoreDocument function on each Document.
         * @param query - the query to perform the search with
         * @return - a mapping of scores to Documents
         */
        multimap<double, string> search(const Document & query) const;

    private:

        vector<Document> _documents;
        size_t _avgDocLength;

        /**
         *
         */
        string shortFilename(const string & filename) const;
};

#endif