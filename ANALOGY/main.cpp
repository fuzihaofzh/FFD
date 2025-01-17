#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <tuple>
#include <random>
#include <omp.h>
#include <cassert>
#include <cstring>
#include <iterator>
#include "set_hash.h"

using namespace std;

static default_random_engine GLOBAL_GENERATOR;
static uniform_real_distribution<double> UNIFORM(0, 1);

typedef tuple<int, int, int> triplet;

struct ScoreTriple{
    double score;
    triplet sro;
    ScoreTriple(double score, triplet sro): score(score), sro(sro){};
    bool operator < (const ScoreTriple& rhs) const
    {
        return score < rhs.score;
    }
};

vector<string> read_first_column(const string& fname) {
    ifstream ifs(fname, ios::in);

    string line;
    string item;
    vector<string> items;

    assert(!ifs.fail());

    while (getline(ifs, line)) {
        stringstream ss(line);
        ss >> item;
        items.push_back(item);
    }
    ifs.close();

    return items;
}

unordered_map<string, int> create_id_mapping(const vector<string>& items) {
    unordered_map<string, int> map;

    for (int i = 0; i < (int) items.size(); i++)
        map[items[i]] = i;

    return map;
}

vector<triplet> create_sros(
        const string& fname,
        const unordered_map<string, int>& ent_map,
        const unordered_map<string, int>& rel_map) {

    ifstream ifs(fname, ios::in);

    string line;
    string s, r, o;
    vector<triplet> sros;

    assert(!ifs.fail());

    while (getline(ifs, line)) {
        stringstream ss(line);
        ss >> s >> r >> o;
        sros.push_back( make_tuple(ent_map.at(s), rel_map.at(r), ent_map.at(o)) );
    }
    ifs.close();

    return sros;
}

vector<vector<double>> uniform_matrix(int m, int n, double l, double h) {
    vector<vector<double>> matrix;
    matrix.resize(m);
    for (int i = 0; i < m; i++)
        matrix[i].resize(n);

    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            matrix[i][j] = (h-l)*UNIFORM(GLOBAL_GENERATOR) + l;

    return matrix;
}

vector<vector<double>> const_matrix(int m, int n, double c) {
    vector<vector<double>> matrix;
    matrix.resize(m);
    for (int i = 0; i < m; i++)
        matrix[i].resize(n);

    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++)
            matrix[i][j] = c;

    return matrix;
}

vector<int> range(int n) {  // 0 ... n-1
    vector<int> v;
    v.reserve(n);
    for (int i = 0; i < n; i++)
        v.push_back(i);
    return v;
}

void l2_normalize(vector<double>& vec) {
    double sq_norm = 0;
    for (unsigned i = 0; i < vec.size(); i++)
        sq_norm += vec[i] * vec[i];
    double norm = sqrt(sq_norm);
    for (unsigned i = 0; i < vec.size(); i++)
        vec[i] /= norm;
}

double sigmoid(double x, double cutoff=30) {
    if (x > +cutoff) return 1.;
    if (x < -cutoff) return 0.;
    return 1./(1.+exp(-x));
}

unordered_map<int, vector<tuple<int, double> > > read_er(string dataset, const unordered_map<string, int>& ent_map, const unordered_map<string, int>& rel_map, string er_type){
    string fname = dataset + "-" + er_type + ".txt";
    ifstream ifs(fname, ios::in);

    string line;
    string car, cdr;
    double prob;
    int type = 0;
    if (er_type == "hr"){
        type = 0;
    }else if (er_type == "rt"){
        type = 1;
    }
    assert(!ifs.fail());
    unordered_map<int, vector<tuple<int, double> > > e_r;
    while (getline(ifs, line)) {
        stringstream ss(line);
        ss >> car >> cdr >> prob;
        if(type == 0){
            e_r[ent_map.find(car)->second].push_back( make_tuple(rel_map.find(cdr)->second, prob));
        }else{
            e_r[rel_map.find(cdr)->second].push_back( make_tuple(ent_map.find(car)->second, prob));
        }
    }
    ifs.close();
    return e_r;
}

void save_triples(const string& fname, vector<triplet> sros, vector<string> ents, vector<string> rels){
    ofstream ofs(fname, ios::out);
    for(const auto & sro: sros){
        ofs<<ents[get<0>(sro)]<<'\t'<<rels[get<1>(sro)]<<'\t'<<ents[get<2>(sro)]<<endl;
    }
    ofs.close();
}

void save_tfscore(const string& fname, vector<tuple<int, double, string, string, string> > & tfscore){
    ofstream ofs(fname, ios::out);
    for(const auto & tfs: tfscore){
        ofs<<get<0>(tfs)<<'\t'<<get<1>(tfs)<<'\t'<<get<2>(tfs)<<'\t'<<get<3>(tfs)<<'\t'<<get<4>(tfs)<<endl;
    }
    ofs.close();
}

class SROBucket {
    unordered_set<int64_t> __sros;
    unordered_map<int64_t, vector<int>> __sr2o;
    unordered_map<int64_t, vector<int>> __or2s;

    int64_t hash(int a, int b, int c) const {
        int64_t x = a;
        x = (x << 20) + b;
        return (x << 20) + c;
    }

    int64_t hash(int a, int b) const {
        int64_t x = a;
        return (x << 32) + b;
    }

public:
    SROBucket(const vector<triplet>& sros) {
        unordered_set<triplet> ssros(sros.begin(), sros.end());
        for (auto sro : ssros) {
            int s = get<0>(sro);
            int r = get<1>(sro);
            int o = get<2>(sro);

            int64_t __sro = hash(s, r, o);
            __sros.insert(__sro);

            int64_t __sr = hash(s, r);
            if (__sr2o.find(__sr) == __sr2o.end())
                __sr2o[__sr] = vector<int>();
            __sr2o[__sr].push_back(o);

            int64_t __or = hash(o, r);
            if (__or2s.find(__or) == __or2s.end())
                __or2s[__or] = vector<int>();
            __or2s[__or].push_back(s);
        }
    }

    bool contains(int a, int b, int c) const {
        return __sros.find( hash(a, b, c) ) != __sros.end();
    }

    vector<int> sr2o(int s, int r) const {
        return __sr2o.at(hash(s,r));
    }

    vector<int> or2s(int o, int r) const {
        return __or2s.at(hash(o,r));
    }
};

// try sample pairs
class NegativeSampler {
    uniform_int_distribution<int> unif_e;
    uniform_int_distribution<int> unif_r;
    default_random_engine generator;

public:
    NegativeSampler(int ne, int nr, int seed) :
        unif_e(0, ne-1), unif_r(0, nr-1), generator(seed) {}

    int random_entity() {
        return unif_e(generator);
    }
    
    int random_relation() {
        return unif_r(generator);
    }
};

class Model {

protected:
    double eta;
    double gamma;
    const double init_b = 1e-2;
    const double init_e = 1e-6;

    vector<vector<double>> E;
    vector<vector<double>> R;
    vector<vector<double>> E_g;
    vector<vector<double>> R_g;

public:

    Model(double eta, double gamma) {
        this->eta = eta;
        this->gamma = gamma;
    }

    void save(const string& fname) {
        ofstream ofs(fname, ios::out);

        for (unsigned i = 0; i < E.size(); i++) {
            for (unsigned j = 0; j < E[i].size(); j++)
                ofs << E[i][j] << ' ';
            ofs << endl;
        }

        for (unsigned i = 0; i < R.size(); i++) {
            for (unsigned j = 0; j < R[i].size(); j++)
                ofs << R[i][j] << ' ';
            ofs << endl;
        }

        ofs.close();
    }

    void load(const string& fname) {
        ifstream ifs(fname, ios::in);

        for (unsigned i = 0; i < E.size(); i++)
            for (unsigned j = 0; j < E[i].size(); j++)
                ifs >> E[i][j];

        for (unsigned i = 0; i < R.size(); i++)
            for (unsigned j = 0; j < R[i].size(); j++)
                ifs >> R[i][j];

        ifs.close();
    }

    void adagrad_update(
            int s,
            int r,
            int o,
            const vector<double>& d_s,
            const vector<double>& d_r,
            const vector<double>& d_o) {

        for (unsigned i = 0; i < E[s].size(); i++) E_g[s][i] += d_s[i] * d_s[i];
        for (unsigned i = 0; i < R[r].size(); i++) R_g[r][i] += d_r[i] * d_r[i];
        for (unsigned i = 0; i < E[o].size(); i++) E_g[o][i] += d_o[i] * d_o[i];

        for (unsigned i = 0; i < E[s].size(); i++) E[s][i] -= eta * d_s[i] / sqrt(E_g[s][i]);
        for (unsigned i = 0; i < R[r].size(); i++) R[r][i] -= eta * d_r[i] / sqrt(R_g[r][i]);
        for (unsigned i = 0; i < E[o].size(); i++) E[o][i] -= eta * d_o[i] / sqrt(E_g[o][i]);
    }

    void train(int s, int r, int o, bool is_positive) {
        vector<double> d_s;
        vector<double> d_r;
        vector<double> d_o;

        d_s.resize(E[s].size());
        d_r.resize(R[r].size());
        d_o.resize(E[o].size());

        double offset = is_positive ? 1 : 0;
        double d_loss = sigmoid(score(s, r, o)) - offset;

        score_grad(s, r, o, d_s, d_r, d_o);

        for (unsigned i = 0; i < d_s.size(); i++) d_s[i] *= d_loss;
        for (unsigned i = 0; i < d_r.size(); i++) d_r[i] *= d_loss;
        for (unsigned i = 0; i < d_o.size(); i++) d_o[i] *= d_loss;

        double gamma_s = gamma / d_s.size();
        double gamma_r = gamma / d_r.size();
        double gamma_o = gamma / d_o.size();

        for (unsigned i = 0; i < d_s.size(); i++) d_s[i] += gamma_s * E[s][i];
        for (unsigned i = 0; i < d_r.size(); i++) d_r[i] += gamma_r * R[r][i];
        for (unsigned i = 0; i < d_o.size(); i++) d_o[i] += gamma_o * E[o][i];

        adagrad_update(s, r, o, d_s, d_r, d_o);
    }

    virtual double score(int s, int r, int o) const = 0;

    virtual void score_grad(
            int s,
            int r,
            int o,
            vector<double>& d_s, 
            vector<double>& d_r, 
            vector<double>& d_o) {};
};

class Evaluator {
    int ne;
    int nr;
    const vector<triplet>& sros;
    const SROBucket& sro_bucket;

public:
    Evaluator(int ne, int nr, const vector<triplet>& sros, const SROBucket& sro_bucket) :
        ne(ne), nr(nr), sros(sros), sro_bucket(sro_bucket) {}

    unordered_map<string, double> evaluate(const Model *model, int truncate) {
        int N = this->sros.size();
        
        if (truncate > 0)
            N = min(N, truncate);

        double mrr_s = 0.;
        double mrr_r = 0.;
        double mrr_o = 0.;

        double mrr_s_raw = 0.;
        double mrr_o_raw = 0.;

        double mr_s = 0.;
        double mr_r = 0.;
        double mr_o = 0.;

        double mr_s_raw = 0.;
        double mr_o_raw = 0.;

        double hits01_s = 0.; 
        double hits01_r = 0.;
        double hits01_o = 0.;

        double hits03_s = 0.;
        double hits03_r = 0.;
        double hits03_o = 0.;

        double hits10_s = 0.;
        double hits10_r = 0.;
        double hits10_o = 0.;

        #pragma omp parallel for reduction(+: mrr_s, mrr_r, mrr_o, mr_s, mr_r, mr_o, \
          hits01_s, hits01_r, hits01_o, hits03_s, hits03_r, hits03_o, hits10_s, hits10_r, hits10_o)
        for (int i = 0; i < N; i++) {
            auto ranks = this->rank(model, sros[i]);

            double rank_s = get<0>(ranks);
            double rank_r = get<1>(ranks);
            double rank_o = get<2>(ranks);
            double rank_s_raw = get<3>(ranks);
            double rank_o_raw = get<4>(ranks);

            mrr_s += 1./rank_s;
            mrr_r += 1./rank_r;
            mrr_o += 1./rank_o;
            mrr_s_raw += 1./rank_s_raw;
            mrr_o_raw += 1./rank_o_raw;

            mr_s += rank_s;
            mr_r += rank_r;
            mr_o += rank_o;
            mr_s_raw += rank_s_raw;
            mr_o_raw += rank_o_raw;

            hits01_s += rank_s <= 01;
            hits01_r += rank_r <= 01;
            hits01_o += rank_o <= 01;

            hits03_s += rank_s <= 03;
            hits03_r += rank_r <= 03;
            hits03_o += rank_o <= 03;

            hits10_s += rank_s <= 10;
            hits10_r += rank_r <= 10;
            hits10_o += rank_o <= 10;
        }

        unordered_map<string, double> info;

        info["mrr_s"] = mrr_s / N;
        info["mrr_r"] = mrr_r / N;
        info["mrr_o"] = mrr_o / N;
        info["mrr_s_raw"] = mrr_s_raw / N;
        info["mrr_o_raw"] = mrr_o_raw / N;

        info["mr_s"] = mr_s / N;
        info["mr_r"] = mr_r / N;
        info["mr_o"] = mr_o / N;
        info["mr_s_raw"] = mr_s_raw / N;
        info["mr_o_raw"] = mr_o_raw / N;

        info["hits01_s"] = hits01_s / N; 
        info["hits01_r"] = hits01_r / N;
        info["hits01_o"] = hits01_o / N;

        info["hits03_s"] = hits03_s / N;
        info["hits03_r"] = hits03_r / N;
        info["hits03_o"] = hits03_o / N;
                                      
        info["hits10_s"] = hits10_s / N;
        info["hits10_r"] = hits10_r / N;
        info["hits10_o"] = hits10_o / N;

        return info;
    }

    // added by maple to predict facts
    vector<triplet> predict_facts(const Model *model, unordered_map<int, vector<tuple<int, double> > >& h_r, unordered_map<int, vector<tuple<int, double> > >& r_t, unordered_set<triplet>& filter_set, vector<string>& ents, vector<string>& rels){
        int mode_r = 1;// 0: full; 1: use h_r
        int mode_t = 1;// 0: full; 1: use r_t
        int mode_p = 0;// 0: not regulize on t; : regulize on t
        size_t pred_tail_cnt = 10;//10
        size_t pred_head_cnt = 50;//50
        // make valid set
        unordered_set<triplet> valid_set(sros.begin(), sros.end());
        unordered_set<int> head_set;
        unordered_map<string, double> info;
        for(const auto & elem: sros){
            head_set.insert(get<0>(elem));
        }
        vector<int> head_vec(head_set.begin(), head_set.end());
        vector<ScoreTriple> scores;
        int cnt = 0;
        #pragma omp declare reduction (merge : std::vector<ScoreTriple> : omp_out.insert(omp_out.end(), omp_in.begin(), omp_in.end()))
        #pragma omp parallel for reduction(merge: scores)
        for(auto ss = head_vec.begin(); ss < head_vec.end(); ++ss){
            #pragma omp critical
            {
                cnt += 1;
                cout<<cnt<<" / "<<head_vec.size()<<"\r";
            }
            vector<ScoreTriple> head_scores;
            head_scores.clear();
            vector<tuple<int, double> > relations;
            vector<tuple<int, double> > tails;
            relations.clear();
            tails.clear();
            for(int i = 0; i < nr; ++i)relations.push_back(make_tuple(i, 1.0));
            for(int i = 0; i < ne; ++i)tails.push_back(make_tuple(i, 1.0));
            if(1 == mode_r) relations = h_r[*ss];
            if (relations.size() == 0)
                cout<<"h_r does not contain some relations."<<endl;
            for(auto rp : relations){
                int rr = get<0>(rp);
                double probr = log(get<1>(rp));
                vector<ScoreTriple> tail_scores;
                tail_scores.clear();
                if(1 == mode_t) {
                    if(r_t.find(rr) != r_t.end()) tails = r_t[rr];
                    else for(int i = 0; i < ne; ++i)tails.push_back(make_tuple(i, 1.0));
                }
                double score_sum = 0;

                vector<double> hrscores;
                hrscores.clear();
                for(auto op : tails){
                    //for (int oo = 0; oo < ne; oo++){
                    int oo = get<0>(op);
                    double probt = log(get<1>(op));
                    double score = model->score(*ss, rr, oo);
                    hrscores.push_back(probr + probt);
                    //score = probr + probt + 0.5 * score;
                    score_sum += exp(score);
                    //double score = model->score(*ss, rr, oo);
                    tail_scores.push_back(ScoreTriple(score, make_tuple(*ss, rr, oo)));
                }
                for(unsigned i = 0; i < tail_scores.size(); ++i){
                    if (mode_p == 1)tail_scores[i].score = 2.0 * log(exp(tail_scores[i].score) / score_sum) + hrscores[i];
                    else tail_scores[i].score = 0.5 * tail_scores[i].score + hrscores[i];
                }

                //std::cout << score_sum<<endl;
                std::nth_element(tail_scores.begin(), tail_scores.begin() + tail_scores.size() - min(tail_scores.size(), pred_tail_cnt), tail_scores.end());
                head_scores.insert(head_scores.end(), tail_scores.rbegin(), tail_scores.rbegin()+min(tail_scores.size(), pred_tail_cnt));
            }
            std::nth_element(head_scores.begin(), head_scores.begin() + head_scores.size() - min(head_scores.size(), pred_head_cnt), head_scores.end());
            scores.insert(scores.end(), head_scores.rbegin(), head_scores.rbegin()+min(head_scores.size(), pred_head_cnt));
        }
        // evaluate
        float tt = 0; float tp = 0;
        vector<triplet> pred_sros;
        double MAP = 0.0, MAP_cnt = 0.0;
        vector<tuple<int, double, string, string, string> > tfscore;
        sort(scores.rbegin(), scores.rend());
        for(const auto & elm: scores){
            bool inTrainSet = (filter_set.find(elm.sro) != filter_set.end());
            bool inTestSet = (valid_set.find(elm.sro) != valid_set.end());
            if(not inTrainSet and pred_sros.size() <= 20000) pred_sros.push_back(elm.sro);
            //if(pred_sros.size() <= 5000) pred_sros.push_back(elm.sro);
            //if(inTrainSet and not inTestSet) continue;
            tt += 1;
            if(inTestSet) {
                tp += 1;
                MAP += tp / tt;
                MAP_cnt += 1;
                //cout<<tt<<",";
            }
            tfscore.push_back(make_tuple(int(inTestSet), elm.score, ents[get<0>(elm.sro)], rels[get<1>(elm.sro)], ents[get<2>(elm.sro)]));
            //if(filter_set.find(elm.sro) != filter_set.end())
                //continue; // in dataset, pass
            //if(tt == 30000) break;
        }
        double P = tp / tt;
        double R = tp / valid_set.size();
        double F1 = 2 * P * R / (P + R);
        MAP /= MAP_cnt;
        cout<<"MAP|precision|recall|F1\n"<<MAP<<"|"<<P<<"|"<<R<<"|"<<F1<<"|"<<tp<<"|"<<tt<<endl;
        save_tfscore("output/predict-score.txt", tfscore);
        random_shuffle(pred_sros.begin(), pred_sros.end());
        return pred_sros;
    }

private:

    tuple<double, double, double, double, double> rank(const Model *model, const triplet& sro) {
        int rank_s = 1;
        int rank_r = 1;
        int rank_o = 1;

        int s = get<0>(sro);
        int r = get<1>(sro);
        int o = get<2>(sro);

        // XXX:
        // There might be degenerated cases when all output scores == 0, leading to perfect but meaningless results.
        // A quick fix is to add a small offset to the base_score.
        double base_score = model->score(s, r, o) - 1e-32;

        for (int ss = 0; ss < ne; ss++)
            if (model->score(ss, r, o) > base_score) rank_s++;

        for (int rr = 0; rr < nr; rr++)
            if (model->score(s, rr, o) > base_score) rank_r++;

        for (int oo = 0; oo < ne; oo++)
            if (model->score(s, r, oo) > base_score) rank_o++;

        int rank_s_raw = rank_s;
        int rank_o_raw = rank_o;

        for (auto ss : sro_bucket.or2s(o, r))
            if (model->score(ss, r, o) > base_score) rank_s--;

        for (auto oo : sro_bucket.sr2o(s, r))
            if (model->score(s, r, oo) > base_score) rank_o--;
        return make_tuple(rank_s, rank_r, rank_o, rank_s_raw, rank_o_raw);
    }
};

void pretty_print(const char* prefix, const unordered_map<string, double>& info) {
    printf("%s  MRR    \t%.2f\t%.2f\t%.2f\n", prefix, 100*info.at("mrr_s"),    100*info.at("mrr_r"),    100*info.at("mrr_o"));
    printf("%s  MRR_RAW\t%.2f\t%.2f\n", prefix, 100*info.at("mrr_s_raw"),    100*info.at("mrr_o_raw"));
    printf("%s  MR     \t%.2f\t%.2f\t%.2f\n", prefix, info.at("mr_s"), info.at("mr_r"), info.at("mr_o"));
    printf("%s  MR_RAW \t%.2f\t%.2f\n", prefix, info.at("mr_s_raw"), info.at("mr_o_raw"));
    printf("%s  Hits@01\t%.2f\t%.2f\t%.2f\n", prefix, 100*info.at("hits01_s"), 100*info.at("hits01_r"), 100*info.at("hits01_o"));
    printf("%s  Hits@03\t%.2f\t%.2f\t%.2f\n", prefix, 100*info.at("hits03_s"), 100*info.at("hits03_r"), 100*info.at("hits03_o"));
    printf("%s  Hits@10\t%.2f\t%.2f\t%.2f\n", prefix, 100*info.at("hits10_s"), 100*info.at("hits10_r"), 100*info.at("hits10_o"));
}

// based on Google's word2vec
int ArgPos(char *str, int argc, char **argv) {
    int a;
    for (a = 1; a < argc; a++) if (!strcmp(str, argv[a])) {
        if (a == argc - 1) {
            printf("Argument missing for %s\n", str);
            exit(1);
        }
        return a;
    }
    return -1;
}


class DistMult : public Model {
    int nh;

public:
    DistMult(int ne, int nr, int nh, double eta, double gamma) : Model(eta, gamma) {
        this->nh = nh;

        E = uniform_matrix(ne, nh, -init_b, init_b);
        R = uniform_matrix(nr, nh, -init_b, init_b);
        E_g = const_matrix(ne, nh, init_e);
        R_g = const_matrix(nr, nh, init_e);
    }

    double score(int s, int r, int o) const {
        double dot = 0;
        for (int i = 0; i < nh; i++)
            dot += E[s][i] * R[r][i] * E[o][i];
        return dot;
    }

    void score_grad(
            int s,
            int r,
            int o,
            vector<double>& d_s, 
            vector<double>& d_r, 
            vector<double>& d_o) {

        for (int i = 0; i < nh; i++) {
            d_s[i] = R[r][i] * E[o][i];
            d_r[i] = E[s][i] * E[o][i]; 
            d_o[i] = E[s][i] * R[r][i];
        }
    }
};


class Complex : public Model {
    int nh;

public:
    Complex(int ne, int nr, int nh, double eta, double gamma) : Model(eta, gamma) {
        assert( nh % 2 == 0 );
        this->nh = nh;

        E = uniform_matrix(ne, nh, -init_b, init_b);
        R = uniform_matrix(nr, nh, -init_b, init_b);
        E_g = const_matrix(ne, nh, init_e);
        R_g = const_matrix(nr, nh, init_e);
    }

    double score(int s, int r, int o) const {
        double dot = 0;

        int nh_2 = nh/2;
        for (int i = 0; i < nh_2; i++) {
            dot += R[r][i]      * E[s][i]      * E[o][i];
            dot += R[r][i]      * E[s][nh_2+i] * E[o][nh_2+i];
            dot += R[r][nh_2+i] * E[s][i]      * E[o][nh_2+i];
            dot -= R[r][nh_2+i] * E[s][nh_2+i] * E[o][i];
        }
        return dot;
    }

    void score_grad(
        int s,
        int r,
        int o,
        vector<double>& d_s, 
        vector<double>& d_r, 
        vector<double>& d_o) {

        int nh_2 = nh/2;
        for (int i = 0; i < nh_2; i++) {
            // re
            d_s[i] = R[r][i] * E[o][i] + R[r][nh_2+i] * E[o][nh_2+i];
            d_r[i] = E[s][i] * E[o][i] + E[s][nh_2+i] * E[o][nh_2+i];
            d_o[i] = R[r][i] * E[s][i] - R[r][nh_2+i] * E[s][nh_2+i];
            // im
            d_s[nh_2+i] = R[r][i] * E[o][nh_2+i] - R[r][nh_2+i] * E[o][i];
            d_r[nh_2+i] = E[s][i] * E[o][nh_2+i] - E[s][nh_2+i] * E[o][i];
            d_o[nh_2+i] = R[r][i] * E[s][nh_2+i] + R[r][nh_2+i] * E[s][i];
        }
    }
};


class Analogy : public Model {
    int nh1;
    int nh2;

public:
    Analogy(int ne, int nr, int nh, int num_scalar, double eta, double gamma) : Model(eta, gamma) {
        this->nh1 = num_scalar;
        this->nh2 = nh - num_scalar;
        assert( this->nh2 % 2 == 0 );

        E = uniform_matrix(ne, nh, -init_b, init_b);
        R = uniform_matrix(nr, nh, -init_b, init_b);
        E_g = const_matrix(ne, nh, init_e);
        R_g = const_matrix(nr, nh, init_e);
    }

    double score(int s, int r, int o) const {
        double dot = 0;

        int i = 0;
        for (; i < nh1; i++)
            dot += E[s][i] * R[r][i] * E[o][i];

        int nh2_2 = nh2/2;
        for (; i < nh1 + nh2_2; i++) {
            dot += R[r][i]       * E[s][i]       * E[o][i];
            dot += R[r][i]       * E[s][nh2_2+i] * E[o][nh2_2+i];
            dot += R[r][nh2_2+i] * E[s][i]       * E[o][nh2_2+i];
            dot -= R[r][nh2_2+i] * E[s][nh2_2+i] * E[o][i];
        }

        return dot;
    }

    void score_grad(
            int s,
            int r,
            int o,
            vector<double>& d_s, 
            vector<double>& d_r, 
            vector<double>& d_o) {

        int i = 0;
        for (; i < nh1; i++) {
            d_s[i] = R[r][i] * E[o][i];
            d_r[i] = E[s][i] * E[o][i]; 
            d_o[i] = E[s][i] * R[r][i];
        }

        int nh2_2 = nh2/2;
        for (; i < nh1 + nh2_2; i++) {
            // re
            d_s[i] = R[r][i] * E[o][i] + R[r][nh2_2+i] * E[o][nh2_2+i];
            d_r[i] = E[s][i] * E[o][i] + E[s][nh2_2+i] * E[o][nh2_2+i];
            d_o[i] = R[r][i] * E[s][i] - R[r][nh2_2+i] * E[s][nh2_2+i];
            // im
            d_s[nh2_2+i] = R[r][i] * E[o][nh2_2+i] - R[r][nh2_2+i] * E[o][i];
            d_r[nh2_2+i] = E[s][i] * E[o][nh2_2+i] - E[s][nh2_2+i] * E[o][i];
            d_o[nh2_2+i] = R[r][i] * E[s][nh2_2+i] + R[r][nh2_2+i] * E[s][i];
        }

    }
};

void train(int ne, int nr, vector<triplet>& sros_tr, vector<triplet>& sros_va, SROBucket &sro_bucket_al, int num_thread, int num_epoch, int eval_freq, Model *model, string model_path, int neg_ratio){
    Evaluator evaluator_va(ne, nr, sros_va, sro_bucket_al);
    Evaluator evaluator_tr(ne, nr, sros_tr, sro_bucket_al);

    // thread-specific negative samplers
    vector<NegativeSampler> neg_samplers;
    for (int tid = 0; tid < num_thread; tid++)
        neg_samplers.push_back( NegativeSampler(ne, nr, rand() ^ tid) );

    int N = sros_tr.size();
    vector<int> pi = range(N);

    clock_t start;
    double elapse_tr = 0;
    //double elapse_ev = 0;
    double best_mrr = 0;

    omp_set_num_threads(num_thread);
    for (int epoch = 0; epoch < num_epoch; epoch++) {
        // evaluation
        if (epoch % eval_freq == 0) {
            start = omp_get_wtime();
            auto info_tr = evaluator_tr.evaluate(model, 2048);
            auto info_va = evaluator_va.evaluate(model, 2048);
            //elapse_ev = omp_get_wtime() - start;

            // save the best model to disk
            double curr_mrr = (info_va["mrr_s"] + info_va["mrr_o"])/2;
            if (curr_mrr > best_mrr) {
                best_mrr = curr_mrr;
                if ( !model_path.empty() )
                    model->save(model_path);
            }

            /*printf("\n");
            printf("            EV Elapse    %f\n", elapse_ev);
            printf("======================================\n");
            pretty_print("TR", info_tr);
            printf("\n");
            pretty_print("VA", info_va);
            printf("\n");
            printf("VA  MRR_BEST    %.2f\n", 100*best_mrr);
            printf("\n");*/
        }

        shuffle(pi.begin(), pi.end(), GLOBAL_GENERATOR);

        start = omp_get_wtime();
        #pragma omp parallel for
        for (int i = 0; i < N; i++) {
            triplet sro = sros_tr[pi[i]];
            int s = get<0>(sro);
            int r = get<1>(sro);
            int o = get<2>(sro);

            int tid = omp_get_thread_num();

            // positive example
            model->train(s, r, o, true);

            // negative examples
            for (int j = 0; j < neg_ratio; j++) {
                int oo = neg_samplers[tid].random_entity();
                int ss = neg_samplers[tid].random_entity();
                int rr = neg_samplers[tid].random_relation();

                // XXX: it is empirically beneficial to carry out updates even if oo == o || ss == s.
                // This might be related to regularization.
                model->train(s, r, oo, false);
                model->train(ss, r, o, false);
                model->train(s, rr, o, false);   // this improves MR slightly
            }
        }
        elapse_tr = omp_get_wtime() - start;
        printf("\rEpoch %03d   TR Elapse    %f", epoch, elapse_tr);
        fflush(stdout);
    }
}

void predict(int ne, int nr, vector<triplet>& sros_tr, vector<triplet>& sros_te, vector<triplet>& sros_va, SROBucket &sro_bucket_al, Model *model, string model_path, string dataset, unordered_map<string, int>& ent_map, unordered_map<string, int>& rel_map, vector<string>& ents, vector<string>& rels){
    Evaluator evaluator_te(ne, nr, sros_te, sro_bucket_al);
    unordered_map<int, vector<tuple<int, double> > > h_r = read_er(dataset, ent_map, rel_map, "hr");
    unordered_map<int, vector<tuple<int, double> > > r_t = read_er(dataset, ent_map, rel_map, "rt");
    model->load(model_path);
    unordered_set<triplet> filter_set;
    filter_set.insert(sros_tr.begin(), sros_tr.end());
    filter_set.insert(sros_va.begin(), sros_va.end());
    vector<triplet> pred_sros = evaluator_te.predict_facts(model, h_r, r_t, filter_set, ents, rels);
    //pred_sros.insert(pred_sros.end(), sros_tr.begin(), sros_tr.end());
    unordered_set<triplet> sros_set(pred_sros.begin(), pred_sros.end());
    vector<triplet> sros_all(sros_set.begin(), sros_set.end());
    save_triples(dataset + "-iter-train.txt", sros_all, ents, rels);//*/
    //auto info_te = evaluator_te.evaluate(model, -1);
    //pretty_print("TE", info_te);
}


int main(int argc, char **argv) {
    // option parser
    string  dataset     =  "FB15k/freebase_mtr100_mte100";
    string  algorithm   =  "Analogy";
    int     embed_dim   =  200;
    double  eta         =  0.1;
    double  gamma       =  1e-3;
    int     neg_ratio   =  6;
    int     num_epoch   =  500;
    int     num_thread  =  32;
    int     eval_freq   =  50;
    string  model_path;
    bool    prediction  = false;
    int     num_scalar  = 100;
    int     feedback    = 0;

    int i;
    if ((i = ArgPos((char *)"-algorithm",  argc, argv)) > 0)  algorithm   =  string(argv[i+1]);
    if ((i = ArgPos((char *)"-embed_dim",  argc, argv)) > 0)  embed_dim   =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-eta",        argc, argv)) > 0)  eta         =  atof(argv[i+1]);
    if ((i = ArgPos((char *)"-gamma",      argc, argv)) > 0)  gamma       =  atof(argv[i+1]);
    if ((i = ArgPos((char *)"-neg_ratio",  argc, argv)) > 0)  neg_ratio   =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-num_epoch",  argc, argv)) > 0)  num_epoch   =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-num_thread", argc, argv)) > 0)  num_thread  =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-eval_freq",  argc, argv)) > 0)  eval_freq   =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-model_path", argc, argv)) > 0)  model_path  =  string(argv[i+1]);
    if ((i = ArgPos((char *)"-dataset",    argc, argv)) > 0)  dataset     =  string(argv[i+1]);
    if ((i = ArgPos((char *)"-prediction", argc, argv)) > 0)  prediction  =  true;
    if ((i = ArgPos((char *)"-num_scalar", argc, argv)) > 0)  num_scalar  =  atoi(argv[i+1]);
    if ((i = ArgPos((char *)"-feedback", argc, argv)) > 0)  feedback  =  atoi(argv[i+1]);

    printf("dataset     =  %s\n", dataset.c_str());
    printf("algorithm   =  %s\n", algorithm.c_str());
    printf("embed_dim   =  %d\n", embed_dim);
    printf("eta         =  %e\n", eta);
    printf("gamma       =  %e\n", gamma);
    printf("neg_ratio   =  %d\n", neg_ratio);
    printf("num_epoch   =  %d\n", num_epoch);
    printf("num_thread  =  %d\n", num_thread);
    printf("eval_freq   =  %d\n", eval_freq);
    printf("model_path  =  %s\n", model_path.c_str());
    printf("num_scalar  =  %d\n", num_scalar);

    vector<string> ents = read_first_column(dataset + "-entities.txt");
    vector<string> rels = read_first_column(dataset + "-relations.txt");

    unordered_map<string, int> ent_map = create_id_mapping(ents);
    unordered_map<string, int> rel_map = create_id_mapping(rels);

    int ne = ent_map.size();
    int nr = rel_map.size();

    vector<triplet> sros_tr = create_sros(dataset + "-train.txt", ent_map, rel_map);
    vector<triplet> sros_va = create_sros(dataset + "-valid.txt", ent_map, rel_map);
    vector<triplet> sros_te = create_sros(dataset + "-test.txt",  ent_map, rel_map);
    vector<triplet> sros_al;

    sros_al.insert(sros_al.end(), sros_tr.begin(), sros_tr.end());
    sros_al.insert(sros_al.end(), sros_va.begin(), sros_va.end());
    sros_al.insert(sros_al.end(), sros_te.begin(), sros_te.end());

    SROBucket sro_bucket_al(sros_al);

    Model *model = NULL;
    if (algorithm == "DistMult")  model = new DistMult(ne, nr, embed_dim, eta, gamma);
    if (algorithm == "Complex")   model = new Complex(ne, nr, embed_dim, eta, gamma);
    if (algorithm == "Analogy")   model = new Analogy(ne, nr, embed_dim, num_scalar, eta, gamma);
    assert(model != NULL);

    if (prediction) {
        predict(ne, nr, sros_tr, sros_te, sros_va, sro_bucket_al, model, model_path, dataset, ent_map, rel_map, ents, rels);
        return 0;
    }

    if(feedback != 0){
        while(feedback--){
            cout<<"feedback time: "<<feedback<<endl;
            cout<<"training data size:"<<sros_tr.size()<<endl;
            train(ne, nr, sros_tr, sros_va, sro_bucket_al, num_thread, num_epoch, eval_freq, model, model_path, neg_ratio);
            predict(ne, nr, sros_tr, sros_te, sros_va, sro_bucket_al, model, model_path, dataset, ent_map, rel_map, ents, rels);
            sros_tr = create_sros(dataset + "-iter-train.txt", ent_map, rel_map);
            sros_al.insert(sros_al.end(), sros_tr.begin(), sros_tr.end());
            sro_bucket_al = SROBucket(sros_al);
        }
        return 0;
    }
    train(ne, nr, sros_tr, sros_va, sro_bucket_al, num_thread, num_epoch, eval_freq, model, model_path, neg_ratio);


    return 0;
}

