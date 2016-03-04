#ifndef DECON_HPP
#define DECON_HPP
#include <vector>
#include <array>
#include <list>
#include <string>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <stack>
#include "Variant.h"
#include "index.hpp"
#include "path.hpp"
#include "vg.hpp"
#include "vg.pb.h"
#include "Fasta.h"
#include "xg.hpp"
#include "position.hpp"
#include "vcfheader.hpp"

/**
* Deconstruct is getting rewritten.
* New functionality:
* -Detect superbubbles and bubbles
* -Fix command line interface.
* -harmonize on XG / raw graph (i.e. deprecate index)
* -Use unroll/DAGify if needed to avoid cycles

** Much of this is taken from Brankovic's
** "Linear-Time Superbubble Identification Algorithm for Genome Assembly"
*/
namespace vg{
    using namespace std;
    using namespace vcfh;
    struct SuperBubble{
      //A vector of topologically-sorted nodes in the superbubble.
      vector<id_t> nodes;
      id_t start_node;
      id_t end_node;
      bool isNested;
    };
    class Deconstructor{
        public:

            Deconstructor();
            Deconstructor(xg::XG x);
						Deconstructor(VG v);
            ~Deconstructor();
            SuperBubble report_superbubble(int start, int end);
            vector<SuperBubble> get_all_superbubbles(VG v);
            int validate_super_bubble(int start, int end);
            vector<int64_t> emit_nodes_on_path_through_superbubble(Path p, vector<SuperBubble> sb);
            vector<int64_t> emit_nodes_off_path_through_superbubble(SuperBubble sb);
            vector<vcflib::Variant> sb_to_variants(SuperBubble sb);

        private:
          xg::XG my_xg;
					VG my_vg;
          vector<SuperBubble> my_super_bubbles;
					//TODO Use arrays to save some memory.
          //NB: All these arrays are the same length, so that a NodeTraversal
          // and index are interchangeable. This is not the way
          // I should have done this.
					vector<bool> entrance_candidates;
					vector<bool> exit_candidates;
					vector<int> previous_entrances;


					vector<NodeTraversal> alt_entrances;
          //This is a deque, and its length may (and will) change.
					deque<NodeTraversal> candidates;
          // This is also a deque but behaves like an array (and in reality, is
          //supposed to be an array).
					deque<NodeTraversal> ord_D;
					bool is_exit(NodeTraversal n);
					bool is_entrance(NodeTraversal n);
          void report(SuperBubble sb);
          int rangemin(vector<int> a, int i, int j);
          int rangemax(vector<int> a, int i, int j);
					void insert_exit(NodeTraversal n);
					void insert_entrance( NodeTraversal n);
          int next(int n);
          NodeTraversal head(deque<NodeTraversal>& cands);
          NodeTraversal tail(deque<NodeTraversal>& cands);
          NodeTraversal delete_tail(deque<NodeTraversal>& cands);


    };
}
#endif
