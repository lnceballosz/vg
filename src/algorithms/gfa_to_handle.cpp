// SPDX-FileCopyrightText: 2014 Erik Garrison
//
// SPDX-License-Identifier: MIT

#include "gfa_to_handle.hpp"
#include "../path.hpp"

namespace vg {
namespace algorithms {

// parse a GFA name into a numeric id
// if all ids are numeric, they will be converted directly with stol
// if all ids are non-numeric, they will get incrementing ids beginning with 1, in order they are visited
// if they are a mix of numeric and non-numeric, the numberic ones will be converted with stol until
// the first non-numeric one is found, then it will revert to using max-id.  this could lead to cases
//
// since non-numeric ids are dependent on the order the nodes are scanned, there is the unfortunate side
// effect that they will be different if read from memory (lex order) or file (file order)
struct IDMapInfo {
    bool numeric_mode = true;
    nid_t max_id = 0;
    unordered_map<string, nid_t> name_to_id;
};
static nid_t parse_gfa_sequence_id(const string& str, IDMapInfo& id_map_info) {
    
    if (id_map_info.name_to_id.count(str)) {
        // already in map, just return
        return id_map_info.name_to_id[str];
    } 

    nid_t node_id = -1;
    if (id_map_info.numeric_mode) {
        if (any_of(str.begin(), str.end(), [](char c) { return !isdigit(c); })) {
            // non-numeric: use max id and add to map
            id_map_info.numeric_mode = false;
        } else {
            node_id = stoll(str);
            if (node_id <= 0) {
                // treat <= 0 as non-numeric
                id_map_info.numeric_mode = false;
            }
        }
    }

    // if numeric, the id was set to stoll above, otherwise we take it from current max
    if (!id_map_info.numeric_mode) {
        node_id = id_map_info.max_id + 1;
    }
    
    id_map_info.max_id = std::max(node_id, id_map_info.max_id);
    id_map_info.name_to_id[str] = node_id;

    return node_id;
}

static void write_gfa_translation(const IDMapInfo& id_map_info, const string& translation_filename) {
    // don't write anything unless we have both an output file and at least one non-trivial mapping
    if (!translation_filename.empty() && !id_map_info.numeric_mode) {
        ofstream trans_file(translation_filename);
        if (!trans_file) {
            throw runtime_error("error:[gfa_to_handle_graph] Unable to open output translation file: " + translation_filename);
        }
        for (const auto& mapping : id_map_info.name_to_id) {
            trans_file << "T\t" << mapping.first << "\t" << mapping.second << "\n";
        }
    }
}

static void validate_gfa_edge(const gfak::edge_elem& e) {
    string not_blunt = ("error:[gfa_to_handle_graph] Can only load blunt-ended GFAs. "
        "Try \"bluntifying\" your graph with a tool like <https://github.com/hnikaein/stark>, or "
        "transitively merge overlaps with a pipeline of <https://github.com/ekg/gimbricate> and "
        "<https://github.com/ekg/seqwish>.");
    if (e.source_begin != e.source_end || e.sink_begin != 0 || e.sink_end != 0) {
        throw GFAFormatError(not_blunt + " Found edge with an overlay: " + e.source_name + "[" + to_string(e.source_begin) + ":" + to_string(e.source_end) + "] -> " + e.sink_name + "[" + to_string(e.sink_begin) + ":" + to_string(e.sink_end) + "]");
    }
    if (!(e.alignment == "0M" || e.alignment == "*" || e.alignment.empty())) {
        throw GFAFormatError(not_blunt + " Found edge with a non-null alignment '" + e.alignment + "'.");
    }
    if (e.source_name.empty()) {
        throw GFAFormatError("error:[gfa_to_handle_graph] Found edge record with missing source name");
    }
    if (e.sink_name.empty()) {
        throw GFAFormatError("error:[gfa_to_handle_graph] Found edge record with missing sink name");
    }
}

static string process_raw_gfa_path_name(const string& path_name_raw)  {
    string processed = path_name_raw;
    processed.erase(remove_if(processed.begin(), processed.end(),
                              [](char c) { return isspace(c); }),
                    processed.end());
    return processed;
}

/// return whether a gfa node has all 3 rGFA tags
/// optionally parse them
static bool gfa_sequence_parse_rgfa_tags(const gfak::sequence_elem& s,
                                         string* out_name = nullptr,
                                         int64_t* out_offset = nullptr,
                                         int64_t* out_rank = nullptr) {
    bool has_sn = false;
    bool has_so = false;
    bool has_sr = false; 
    for (size_t i = 0 ; i < s.opt_fields.size() && (!has_sn || !has_so || !has_sr); ++i) {
        if (s.opt_fields[i].key == "SN" && s.opt_fields[i].type == "Z") {
            has_sn = true;
            if (out_name) {
                *out_name = s.opt_fields[i].val;
            }
        } else if (s.opt_fields[i].key == "SO" && s.opt_fields[i].type == "i") {
            has_so = true;
            if (out_offset) {
                *out_offset = stol(s.opt_fields[i].val);
            }
        } else if (s.opt_fields[i].key == "SR" && s.opt_fields[i].type == "i") {
            has_sr = true;
            if (out_rank) {
                *out_rank = stol(s.opt_fields[i].val);
            }
        }
    }
    return has_sn && has_so && has_sr;
}

static bool gfa_to_handle_graph_in_memory(istream& in, MutableHandleGraph* graph,
                                          gfak::GFAKluge& gg, IDMapInfo& id_map_info) {
    if (!in) {
        throw std::ios_base::failure("error:[gfa_to_handle_graph] Couldn't open input stream");
    }
    gg.parse_gfa_file(in);

    // create nodes
    bool has_rgfa_tags = false;
    for (const auto& seq_record : gg.get_name_to_seq()) {
        graph->create_handle(seq_record.second.sequence, parse_gfa_sequence_id(seq_record.first, id_map_info));
        has_rgfa_tags = has_rgfa_tags || gfa_sequence_parse_rgfa_tags(seq_record.second);
    }
    
    // create edges
    for (const auto& links_record : gg.get_seq_to_edges()) {
        for (const auto& edge : links_record.second) {
            validate_gfa_edge(edge);
            // note: we're counting on implementations de-duplicating edges
            handle_t a = graph->get_handle(parse_gfa_sequence_id(edge.source_name, id_map_info), !edge.source_orientation_forward);
            handle_t b = graph->get_handle(parse_gfa_sequence_id(edge.sink_name, id_map_info), !edge.sink_orientation_forward);
            graph->create_edge(a, b);
        }
    }
    return has_rgfa_tags;
}

static bool gfa_to_handle_graph_on_disk(const string& filename, MutableHandleGraph* graph,
                                        bool try_id_increment_hint, gfak::GFAKluge& gg, IDMapInfo& id_map_info) {
    
    // adapted from
    // https://github.com/vgteam/odgi/blob/master/src/gfa_to_handle.cpp
    
    if (try_id_increment_hint) {
        
        // find the minimum ID
        nid_t min_id = numeric_limits<nid_t>::max();
        gg.for_each_sequence_line_in_file(filename.c_str(), [&](gfak::sequence_elem s) {
                min_id = std::min(min_id, parse_gfa_sequence_id(s.name, id_map_info));
            });
        
        if (min_id != numeric_limits<nid_t>::max()) {
            // we found the min, set it as the increment
            graph->set_id_increment(min_id);
        }
    }
    
    // add in all nodes
    bool has_rgfa_tags = false;
    gg.for_each_sequence_line_in_file(filename.c_str(), [&](gfak::sequence_elem s) {        
            graph->create_handle(s.sequence, parse_gfa_sequence_id(s.name, id_map_info));
            has_rgfa_tags = has_rgfa_tags || gfa_sequence_parse_rgfa_tags(s);
    });
    
    // add in all edges
    gg.for_each_edge_line_in_file(filename.c_str(), [&](gfak::edge_elem e) {
        validate_gfa_edge(e);
        handle_t a = graph->get_handle(parse_gfa_sequence_id(e.source_name, id_map_info), !e.source_orientation_forward);
        handle_t b = graph->get_handle(parse_gfa_sequence_id(e.sink_name, id_map_info), !e.sink_orientation_forward);
        graph->create_edge(a, b);
    });
    return has_rgfa_tags;
}


/// Parse nodes and edges and load them into the given GFAKluge.
/// If the input is a seekable file, filename will be filled in and unseekable will be nullptr.
/// If the input is not a seekable file, filename may be filled in, and unseekable will be set to a stream to read from.
/// Returns true if any "SN" rGFA tags are found in the graph nodes
static bool gfa_to_handle_graph_load_graph(const string& filename, istream* unseekable, MutableHandleGraph* graph,
                                           bool try_id_increment_hint, gfak::GFAKluge& gg, IDMapInfo& id_map_info) {
    
    if (graph->get_node_count() > 0) {
        throw invalid_argument("error:[gfa_to_handle_graph] Must parse GFA into an empty graph");
    }
    bool has_rgfa_tags = false;
    if (!unseekable) {
        // Do the from-disk path
        has_rgfa_tags = gfa_to_handle_graph_on_disk(filename, graph, try_id_increment_hint, gg, id_map_info);
    } else {
        // Do the path for streams
        
        if (try_id_increment_hint) {
            // The ID increment hint can't be done.
            cerr << "warning:[gfa_to_handle_graph] Skipping node ID increment hint because input stream for GFA does not support seeking. "
                 << "If performance suffers, consider using an alternate graph implementation or reading GFA from hard disk." << endl;
        }
        
        has_rgfa_tags = gfa_to_handle_graph_in_memory(*unseekable, graph, gg, id_map_info);
    }
    return has_rgfa_tags;
}

/// After the given GFAKluge has been populated with nodes and edges, load path information.
/// If the input is a seekable file, filename will be filled in and unseekable will be nullptr.
/// If the input is not a seekable file, filename may be filled in, and unseekable will be set to a stream to read from.
static void gfa_to_handle_graph_add_paths(const string& filename, istream* unseekable, MutablePathHandleGraph* graph,
                                          gfak::GFAKluge& gg, IDMapInfo& id_map_info) {
                                   
                                   
    if (!unseekable) {
        // Input is from a seekable file on disk.
        
        // add in all paths
        gg.for_each_path_element_in_file(filename.c_str(), [&](const string& path_name_raw,
                                                               const string& node_id,
                                                               bool is_rev,
                                                               const string& cigar,
                                                               bool is_empty,
                                                               bool is_circular) {
            // remove white space in path name
            // TODO: why?
            string path_name = process_raw_gfa_path_name(path_name_raw);
            
            // get either the existing path handle or make a new one
            path_handle_t path;
            if (!graph->has_path(path_name)) {
                path = graph->create_path_handle(path_name, is_circular);
            } else {
                path = graph->get_path_handle(path_name);
            }
            
            // add the step
            handle_t step = graph->get_handle(parse_gfa_sequence_id(node_id, id_map_info), is_rev);
            graph->append_step(path, step);
        });
    } else {
        
        // gg will have parsed the GFA file in the non-path part of the algorithm
        // No reading to do.
        
        // create paths
        for (const auto& path_record : gg.get_name_to_path()) {
            
            // process this to match the disk backed implementation
            // TODO: why?
            string path_name = process_raw_gfa_path_name(path_record.first);
            path_handle_t path = graph->create_path_handle(path_name);
            
            for (size_t i = 0; i < path_record.second.segment_names.size(); ++i) {
                handle_t step = graph->get_handle(parse_gfa_sequence_id(path_record.second.segment_names.at(i), id_map_info),
                                                  !path_record.second.orientations.at(i));
                graph->append_step(path, step);
            }
        }
    }
    
    
}

/// add paths from the optional rgfa tags on sequence nodes (SN: name SO: offset SR: rank)
/// max_rank selects which ranks to consider. Usually, only rank-0 paths are full paths while rank > 0 are subpaths
static void gfa_to_handle_graph_add_rgfa_paths(const string filename, istream* unseekable, MutablePathHandleGraph* graph,
                                               gfak::GFAKluge& gg, IDMapInfo& id_map_info,
                                               int64_t max_rank) {

    // build up paths in memory using a plain old stl structure
    // maps path-name to <rank, vector<node_id, offset>>
    unordered_map<string, pair<int64_t, vector<pair<nid_t, int64_t>>>> path_map;

    string rgfa_name;
    int64_t rgfa_offset;
    int64_t rgfa_rank;

    function<void(const gfak::sequence_elem&)> update_rgfa_path = [&](const gfak::sequence_elem& s) {
        if (gfa_sequence_parse_rgfa_tags(s, &rgfa_name, &rgfa_offset, &rgfa_rank) && rgfa_rank <= max_rank) {
            pair<int64_t, vector<pair<nid_t, int64_t>>>& val = path_map[rgfa_name];
            if (!val.second.empty()) {
                if (val.first != rgfa_rank) {
                    cerr << "warning:[gfa_to_handle_graph] Ignoring rGFA tags for sequence " << s.name
                         << " because they identify it as being on path " << rgfa_name << " with rank " << rgfa_rank
                         << " but a path with that name has already been found with a different rank (" << val.first << ")" << endl;
                    return;
                }
            } else {
                val.first = rgfa_rank;
            }
            nid_t seq_id = parse_gfa_sequence_id(s.name, id_map_info);
            val.second.push_back(make_pair(seq_id, rgfa_offset));
        }
    };
    
    if (!unseekable) {
        // Input is from a seekable file on disk.
        gg.for_each_sequence_line_in_file(filename.c_str(), [&](gfak::sequence_elem s) {
                update_rgfa_path(s);
            });
    } else {        
        // gg will have parsed the GFA file in the non-path part of the algorithm
        // No reading to do.
        for (const auto& seq_record : gg.get_name_to_seq()) {
            update_rgfa_path(seq_record.second);
        }
    }

    for (auto& path_offsets : path_map) {
        const string& name = path_offsets.first;
        int64_t rank = path_offsets.second.first;
        vector<pair<nid_t, int64_t>>& node_offsets = path_offsets.second.second;
        if (graph->has_path(name)) {
            cerr << "warning:[gfa_to_handle_graph] Ignoring rGFA tags for path " << name << " as a path with that name "
                 << "has already been imported from a P-line" << endl;
            continue;
        }
        // sort by offset
        sort(node_offsets.begin(), node_offsets.end(),
             [](const pair<nid_t, int64_t>& o1, const pair<nid_t, int64_t>& o2) { return o1.second < o2.second;});

        // make a path for each run of contiguous offsets
        int64_t prev_sequence_size;
        path_handle_t path_handle;
        for (int64_t i = 0; i < node_offsets.size(); ++i) {
            bool contiguous = i > 0 && node_offsets[i].second == node_offsets[i-1].second + prev_sequence_size;
            if (!contiguous) {
                // should probably detect and throw errors if overlap (as opposed to gap)
                string path_chunk_name = node_offsets[i].second == 0 ? name : Paths::make_subpath_name(name, node_offsets[i].second);
                path_handle = graph->create_path_handle(path_chunk_name);
            }
            handle_t step = graph->get_handle(node_offsets[i].first, false);
            graph->append_step(path_handle, step);
            prev_sequence_size = graph->get_length(step);
        }
    }    
}

void gfa_to_handle_graph(const string& filename, MutableHandleGraph* graph,
                         bool try_from_disk, bool try_id_increment_hint,
                         const string& translation_filename) {

    // What stream should we read from (isntead of opening the file), if any?
    istream* unseekable = nullptr;
    
    // If we open a file, it will live here.
    unique_ptr<ifstream> opened;
    
    if (filename == "-") {
        // Read from standard input
        unseekable = &cin;
    } else if (!try_from_disk) {
        // The file may be seekable actually, but we don't want to use the
        // seekable-file codepath for some reason.
        opened = make_unique<ifstream>(filename);
        if (!opened) {
            throw std::ios_base::failure("error:[gfa_to_handle_graph] Couldn't open file " + filename);
        }
        unseekable = opened.get();
    }
    
    gfak::GFAKluge gg;
    IDMapInfo id_map_info;
    gfa_to_handle_graph_load_graph(filename, unseekable, graph, try_id_increment_hint, gg, id_map_info);

    write_gfa_translation(id_map_info, translation_filename);
}


void gfa_to_path_handle_graph(const string& filename, MutablePathMutableHandleGraph* graph,
                              bool try_from_disk, bool try_id_increment_hint,
                              int64_t max_rgfa_rank, const string& translation_filename) {
    
    
    // What stream should we read from (isntead of opening the file), if any?
    istream* unseekable = nullptr;
    
    // If we open a file, it will live here.
    unique_ptr<ifstream> opened;
    
    if (filename == "-") {
        // Read from standard input
        unseekable = &cin;
    } else if (!try_from_disk) {
        // The file may be seekable actually, but we don't want to use the
        // seekable-file codepath for some reason.
        opened = make_unique<ifstream>(filename);
        if (!opened) {
            throw std::ios_base::failure("error:[gfa_to_handle_graph] Couldn't open file " + filename);
        }
        unseekable = opened.get();
    }
    
    gfak::GFAKluge gg;
    IDMapInfo id_map_info;
    bool has_rgfa_tags = gfa_to_handle_graph_load_graph(filename, unseekable, graph, try_id_increment_hint, gg, id_map_info);
    
    // TODO: Deduplicate everything other than this line somehow.
    gfa_to_handle_graph_add_paths(filename, unseekable, graph, gg, id_map_info);

    if (has_rgfa_tags) {
        gfa_to_handle_graph_add_rgfa_paths(filename, unseekable, graph, gg, id_map_info, max_rgfa_rank);
    }

    write_gfa_translation(id_map_info, translation_filename);
}

void gfa_to_path_handle_graph_in_memory(istream& in,
                                        MutablePathMutableHandleGraph* graph,
                                        int64_t max_rgfa_rank) {
    gfak::GFAKluge gg;
    IDMapInfo id_map_info;
    bool has_rgfa_tags = gfa_to_handle_graph_load_graph("", &in, graph, false, gg, id_map_info);
    gfa_to_handle_graph_add_paths("", &in, graph, gg, id_map_info);
    if (has_rgfa_tags) {
        gfa_to_handle_graph_add_rgfa_paths("", &in, graph, gg, id_map_info, max_rgfa_rank);
    }
    
}

}
}
