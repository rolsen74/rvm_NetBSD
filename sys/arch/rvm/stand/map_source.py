#!/usr/bin/env python3
"""Adapt the MI map's callback initializer for the bootstrap FHR compiler.

The compiler cannot emit function pointers in static C initializers yet.
Assign these two callbacks at runtime before the real MI map setup. Keep
the source tree's MI implementation unchanged; refuse unexpected input.
"""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
declaration = '''static const rb_tree_ops_t uvm_map_tree_ops = {
	.rbto_compare_nodes = uvm_map_compare_nodes,
	.rbto_compare_key = uvm_map_compare_key,
	.rbto_node_offset = offsetof(struct vm_map_entry, rb_node),
	.rbto_context = NULL
};'''
replacement = '''static rb_tree_ops_t uvm_map_tree_ops;
static void
rvm_map_tree_callbacks(void)
{
	uvm_map_tree_ops.rbto_compare_nodes = uvm_map_compare_nodes;
	uvm_map_tree_ops.rbto_compare_key = uvm_map_compare_key;
	uvm_map_tree_ops.rbto_node_offset = offsetof(struct vm_map_entry, rb_node);
	uvm_map_tree_ops.rbto_context = NULL;
}'''
setup = '\trb_tree_init(&map->rb_tree, &uvm_map_tree_ops);'
if source.count(declaration) != 1 or source.count(setup) != 1:
    raise SystemExit('MI map callback layout changed; review bootstrap adapter')
source = source.replace(declaration, replacement)
source = source.replace(setup, '\trvm_map_tree_callbacks();\n' + setup)
source += '''
/* Identify the one cache supported by the standalone entry store. */
pool_cache_t rvm_map_cache_address(void) { return &uvm_map_entry_cache; }
'''
Path(sys.argv[2]).write_text(source)
