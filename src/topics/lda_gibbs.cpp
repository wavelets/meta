/**
 * @file lda_gibbs.cpp
 * @author Chase Geigle
 */

#include <algorithm>
#include <cmath>

#include "index/postings_data.h"
#include "topics/lda_gibbs.h"
#include "util/progress.h"

namespace meta
{
namespace topics
{

lda_gibbs::lda_gibbs(std::shared_ptr<index::forward_index> idx,
                     uint64_t num_topics, double alpha, double beta)
    : lda_model{std::move(idx), num_topics}, alpha_{alpha}, beta_{beta}
{
    std::random_device dev;
    rng_.seed(dev());
}

void lda_gibbs::run(uint64_t num_iters, double convergence /* = 1e-6 */)
{
    initialize();
    double likelihood = corpus_likelihood();
    std::stringstream ss;
    ss << "Initialization log likelihood: " << likelihood;
    std::string spacing(std::max<int>(0, 80 - ss.tellp()), ' ');
    ss << spacing;
    LOG(progress) << '\r' << ss.str() << '\n' << ENDLG;

    for (uint64_t i = 0; i < num_iters; ++i)
    {
        perform_iteration(i + 1);
        double likelihood_update = corpus_likelihood();
        double ratio = std::fabs((likelihood - likelihood_update) / likelihood);
        likelihood = likelihood_update;
        std::stringstream ss;
        ss << "Iteration " << i + 1 << " log likelihood: " << likelihood;
        std::string spacing(std::max<int>(0, 80 - ss.tellp()), ' ');
        ss << spacing;
        LOG(progress) << '\r' << ss.str() << '\n' << ENDLG;
        if (ratio <= convergence)
        {
            LOG(progress) << "Found convergence after " << i + 1
                          << " iterations!\n" << ENDLG;
            break;
        }
    }
    LOG(info) << "Finished maximum iterations, or found convergence!" << ENDLG;
}

topic_id lda_gibbs::sample_topic(term_id term, doc_id doc)
{
    std::vector<double> weights(num_topics_);
    for (topic_id j{0}; j < weights.size(); ++j)
        weights[j] = compute_probability(term, doc, j);
    std::discrete_distribution<uint64_t> dist(weights.begin(), weights.end());
    return topic_id{dist(rng_)};
}

double lda_gibbs::compute_probability(term_id term, doc_id doc,
                                      topic_id topic) const
{
    return compute_term_topic_probability(term, topic)
           * compute_doc_topic_probability(doc, topic);
}

double lda_gibbs::compute_term_topic_probability(term_id term,
                                                 topic_id topic) const
{
    return (count_term(term, topic) + beta_)
           / (count_topic(topic) + num_words_ * beta_);
}

double lda_gibbs::compute_doc_topic_probability(doc_id doc,
                                                topic_id topic) const
{
    return (count_doc(doc, topic) + alpha_)
           / (count_doc(doc) + num_topics_ * alpha_);
}

double lda_gibbs::count_term(term_id term, topic_id topic) const
{
    auto it = topic_term_count_.find(topic);
    if (it == topic_term_count_.end())
        return 0;
    auto iit = it->second.find(term);
    if (iit == it->second.end())
        return 0;
    return iit->second;
}

double lda_gibbs::count_topic(topic_id topic) const
{
    auto it = topic_count_.find(topic);
    if (it == topic_count_.end())
        return 0;
    return it->second;
}

double lda_gibbs::count_doc(doc_id doc, topic_id topic) const
{
    auto it = doc_topic_count_.find(doc);
    if (it == doc_topic_count_.end())
        return 0;
    auto iit = it->second.find(topic);
    if (iit == it->second.end())
        return 0;
    return iit->second;
}

double lda_gibbs::count_doc(doc_id doc) const
{
    return idx_->doc_size(doc);
}

void lda_gibbs::initialize()
{
    perform_iteration(0, true);
}

void lda_gibbs::perform_iteration(uint64_t iter, bool init /* = false */)
{
    std::string str;
    if (init)
        str = "Initialization: ";
    else
        str = "Iteration " + std::to_string(iter) + ": ";
    printing::progress progress{str, idx_->num_docs()};
    progress.print_endline(false);
    for (const auto& i : idx_->docs())
    {
        progress(i);
        uint64_t n = 0; // term number within document---constructed
                        // so that each occurrence of the same term
                        // can still be assigned a different topic
        for (const auto& freq : idx_->search_primary(i)->counts())
        {
            for (uint64_t j = 0; j < freq.second; ++j)
            {
                topic_id old_topic = doc_word_topic_[i][n];
                // don't include current topic assignment in
                // probability calculation
                if (!init)
                    decrease_counts(old_topic, freq.first, i);

                // sample a new topic assignment
                topic_id topic = sample_topic(freq.first, i);
                doc_word_topic_[i][n] = topic;

                // increase counts
                increase_counts(topic, freq.first, i);
                n += 1;
            }
        }
    }
}

void lda_gibbs::decrease_counts(topic_id topic, term_id term, doc_id doc)
{
    // decrease topic_term_count_ for the given assignment
    auto& tt_count = topic_term_count_.at(topic).at(term);
    if (tt_count == 1)
        topic_term_count_.at(topic).erase(term);
    else
        tt_count -= 1;

    // decrease doc_topic_count_ for the given assignment
    auto& dt_count = doc_topic_count_.at(doc).at(topic);
    if (dt_count == 1)
        doc_topic_count_.at(doc).erase(topic);
    else
        dt_count -= 1;

    // decrease topic count
    auto& tc = topic_count_.at(topic);
    if (tc == 1)
        topic_count_.erase(topic);
    else
        tc -= 1;
}

void lda_gibbs::increase_counts(topic_id topic, term_id term, doc_id doc)
{
    topic_term_count_[topic][term] += 1;
    doc_topic_count_[doc][topic] += 1;
    topic_count_[topic] += 1;
}

double lda_gibbs::corpus_likelihood() const
{
    double likelihood = num_topics_ * (std::lgamma(num_words_ * beta_)
                                       - num_words_ * std::lgamma(beta_));
    for (topic_id j{0}; j < num_topics_; ++j)
    {
        for (const auto& d_id : idx_->docs())
        {
            for (const auto& freq : idx_->search_primary(d_id)->counts())
            {
                likelihood += freq.second
                              * std::lgamma(count_term(freq.first, j) + beta_);
            }
        }
        likelihood -= std::lgamma(count_topic(j) + num_words_ * beta_);
    }
    return likelihood;
}
}
}
