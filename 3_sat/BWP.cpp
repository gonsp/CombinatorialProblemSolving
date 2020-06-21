#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <array>

#include "../include/BWPInstance.hh"

using namespace std;

typedef int var_id;
typedef vector<vector<var_id>> CNF;
typedef vector<bool> Assignment;

void write_codification(int num_vars, const CNF& cnf) {
    ofstream codification("encoded.cnf");
    codification << "c test" << endl;
    codification << "p cnf " << num_vars << " " << cnf.size() << endl;
    for(int i = 0; i < cnf.size(); ++i) {
        for(int j = 0; j < cnf[i].size(); ++j) {
            codification << cnf[i][j] << " ";
        }
        codification << " 0" << endl;
    }
    codification.close();
}

Assignment find_assignment(int num_vars, const CNF& cnf) {
    write_codification(num_vars, cnf);

    Assignment assignment(num_vars );

    cerr << "Calling solver" << endl;
    FILE *output = popen("lingeling -o solution encoded.cnf", "r");
    if(!output){
        exit(1);
    }
    const int MAX_BUFFER = 2048;
    array<char, MAX_BUFFER> buffer;
    while(!feof(output)) {
        if(fgets(buffer.data(), MAX_BUFFER, output) != NULL) {
            istringstream iss(string(buffer.data()));
            char c;
            iss >> c;
            if(c == 's') {
                string result;
                iss >> result;
                if(result == "UNSATISFIABLE") {
                    return Assignment(0);
                }
            }
            if(c == 'v') {
                int var;
                while(iss >> var) {
                    assignment[abs(var)] = var > 0;
                }
            }
        }
    }
    pclose(output);
    cerr << "Solver finished" << endl;

    return assignment;
}

struct EncodedProblem {
    vector<var_id> dirs;
    vector<vector<var_id>> pos_x;
    vector<vector<var_id>> pos_y;

    CNF cnf;
    int num_vars = 0;
};

#define width(i) (dir * instance.boxes[i].first + (1 - dir) * instance.boxes[i].second)
#define height(i) (dir * instance.boxes[i].second + (1 - dir) * instance.boxes[i].first)

EncodedProblem encode(BWPInstance instance, int L) {
    int N = instance.N;
    int W = instance.W;
    EncodedProblem problem;
    problem.dirs = vector<var_id>(N);
    problem.pos_x = vector<vector<var_id>>(N);
    problem.pos_y = vector<vector<var_id>>(N);

    vector<var_id>& dirs = problem.dirs;
    vector<vector<var_id>>& pos_x = problem.pos_x;
    vector<vector<var_id>>& pos_y = problem.pos_y;
    CNF& cnf = problem.cnf;
    int& num_vars = problem.num_vars;

    // Initializing variables
    for(int i = 0; i < N; ++i) {
        dirs[i] = ++num_vars;
        pos_x[i] = vector<var_id>(W);
        pos_y[i] = vector<var_id>(L);
        for(int x = 0; x < W; ++x) {
            pos_x[i][x] = ++num_vars;
        }
        for(int y = 0; y < L; ++y) {
            pos_y[i][y] = ++num_vars;
        }
    }

    cerr << "Num variables = " << num_vars << endl;

    // Valid values
    for(int i = 0; i < N; ++i) {
        vector<var_id> ALO_pos_x(W);
        for(int x = 0; x < W; ++x) {
            ALO_pos_x[x] = pos_x[i][x];
        }
        cnf.push_back(ALO_pos_x);
        vector<var_id> ALO_pos_y(L);
        for(int y = 0; y < L; ++y) {
            ALO_pos_y[y] = pos_y[i][y];
        }
        cnf.push_back(ALO_pos_y);
    }


    cerr << cnf.size() << " clauses for valid constraints" << endl;
    int prev = cnf.size();

    // Position clauses
    for(int i = 0; i < N; ++i) {
        for(int dir = 0; dir <= 1; ++dir) {
            for(int x = W - width(i) + 1; x < W; ++x) {
                cnf.push_back({-dirs[i], -pos_x[i][x]});
            }
            for(int y = L - height(i) + 1; y < L; ++y) {
                cnf.push_back({-dirs[i], -pos_y[i][y]});
            }
        }
    }


    cerr << cnf.size() - prev << " clauses for position constraints" << endl;
    prev = cnf.size();

    // Overlapping clauses
    for(int i = 0; i < N; ++i) {
        for(int dir = 0; dir <= 1; ++dir) {
            for(int j = 0; j < N; ++j) {
                if(i != j) {
                    for(int x = 0; x <= W - width(i); ++x) {
                        for(int y = 0; y <= L - height(i); ++y) {
                            for(int p = 0; p < width(i); ++p) {
                                for(int q = 0; q < height(i); ++q) {
                                    cnf.push_back({-dirs[i], -pos_x[i][x], -pos_y[i][y], -pos_x[j][x + p], -pos_y[j][y + q]});
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    cerr << cnf.size() - prev << " clauses for overlapping constraints" << endl;
    prev = cnf.size();

    cerr << cnf.size() << " total clauses" << endl;

    return problem;
}

int var_to_value(const vector<var_id>& vars, const Assignment& assignment) {
    for(int pos = 0; pos < vars.size(); ++pos) {
        if(assignment[vars[pos]] > 0) {
            return pos;
        }
    }
    throw "Invalid assignment";
}

void print_solution(const BWPInstance& instance, const EncodedProblem& problem, const Assignment& assignment, int L) {
    cout << L << endl;
    for(int i = 0; i < instance.N; ++i) {
        int dir = problem.dirs[i];
        int x = var_to_value(problem.pos_x[i], assignment);
        int y = var_to_value(problem.pos_y[i], assignment);
        cout << x << " " << y << " " << x + width(i) - 1 << " " << y + height(i) - 1 << endl;
    }
}

int main() {
    BWPInstance instance;
    instance.read();

    EncodedProblem prev_problem;
    Assignment prev_assignment;
    int L = instance.L_upper_bound;
    // To find an assignment for a F \in SAT is easier than to determine that F \notin SAT
    // So it's better to ask many times for an existing assignment and once for a non-existing one
    // rather than to ask fewer times but half of them asking for a non-existing assignment.
    // So we do a linear search instead of a binary one, 
    // and starting from an upper bound of L instead of lower bound.
    while(true) {
        EncodedProblem problem = encode(instance, L);
        Assignment assignment = find_assignment(problem.num_vars, problem.cnf);
        if(L == instance.L_lower_bound || assignment.size() == 0) {
            print_solution(instance, prev_problem, prev_assignment, L+1);
            break;
        }
        prev_problem = problem;
        prev_assignment = assignment;
        --L;
    }
}
