#include "grid_to_oel32.h"
#include <gstream/grid_format/dense_block.h>
#include <gstream/grid_format/sparse_block.h>
#include <gstream/grid_format/extent.h>
#include <gstream/grid_format/detail/gridgen_utility.h>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <ash/utility/prompt.h>
#include <ash/detail/malloc.h>

namespace fs = ::boost::filesystem;

grid_to_oel32::grid_to_oel32(config_t const& cfg) {
    _cfg = cfg;
    _input_path = _cfg.grid_input_path;
    _output_path = _cfg.el32_output_path;
    assert(_open_grid_stream() == true);
    _row = _gs.grid_info().dim.y;
    _col = _gs.grid_info().dim.x;
    _base_shard_size = _gs.grid_info().base_shard_size;
    input_buf = ash::aligned_malloc(_base_shard_size, gstream::DiskIoSectorSize);
    _el_ofs = new el32_ofstream[_col];
    for (uint32_t i = 0; i < _col; i++)
        _el_ofs[i].init_buf(_cfg.output_buf_size);
    _total_edge = 0;
}

grid_to_oel32::~grid_to_oel32() {
    _gs.close();
    ash::aligned_free(input_buf);
    delete[] _el_ofs;
}

bool grid_to_oel32::run() {
    if (!_cfg.is_dense_format) {
        printf("Not support sparse format yet.\n");
        return false;
    }
    if (!el32_check_output_directory(_output_path, _row)) {
        return false;
    }

    using namespace gstream;
    using namespace grid_format;

    for (uint32_t i = 0; i < _row; i++) {
        _row_grid_ID = i;
        _init_output_stream();
        for (uint32_t j = 0; j < _col; j++) {
            gbid_t gbid = {i, j};
            printf("Progress: [GBID: %u, %u]\n", gbid.row, gbid.col);
            _make_candidate_shard(gbid);
            _progress_shard(gbid);
            _clear();
        }
        _close_output_stream();
    }

    return true;
}

uint64_t grid_to_oel32::get_total_edge() {
    return _total_edge;
}

bool grid_to_oel32::_open_grid_stream() {
    namespace fs = boost::filesystem;
    using namespace gstream;
    using namespace grid_format;
    if (!fs::exists(_input_path)) {
        printf("grid input path is not found!\n");
        return false;
    }

    grid_stream2::config_t gs_cfg;
    gs_cfg.grid_dir  = _input_path.data();
    gs_cfg.grid_name = _cfg.grid_name.data();
    gs_cfg.extent_size = 0;
    gs_cfg.opt = GSTREAM_USE_DIRECT_IO;

    if (!_gs.open(gs_cfg)) {
        printf("grid open fail!\n");
        return false;
    }
    return true;
}

void grid_to_oel32::_make_candidate_shard(gstream::grid_format::gbid_t const& gbid) {
    using namespace gstream;
    using namespace grid_format;

    stree_t const* stree = _gs.stree(gbid);
    stree->dfs([&](stree_node_ptr snode) {
        if (snode->header.node_type == qtree_node_type::InternalNode)
            return;
        _candidate_shard.push_back({snode->leaf, nullptr});
    });

    sort(_candidate_shard.begin(), _candidate_shard.end(), shard_info_row_cmp());
}

void grid_to_oel32::_init_output_stream() {
    for (uint32_t i = 0; i < _col; i++) {
        _el_ofs[i].init_stream(el32_make_optimal_path(_output_path, _row_grid_ID, i));
    }
}

void grid_to_oel32::_close_output_stream() {
    for (uint32_t i = 0; i < _col; i++) {
        _el_ofs[i].close_stream();
    }
}