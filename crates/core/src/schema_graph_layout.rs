use choscordb_driver_api::ObjectGraph;
use std::collections::HashMap;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GraphCell {
    pub column: u32,
    pub row: u32,
    pub group: Option<u32>,
}

struct Block {
    members: Vec<usize>,
    layers: Vec<Vec<usize>>,
}

fn layer(
    table: usize,
    references: &[Vec<usize>],
    layers: &mut [Option<usize>],
    visiting: &mut [bool],
) -> usize {
    if let Some(known) = layers[table] {
        return known;
    }
    visiting[table] = true;
    let mut deepest = None;
    for &target in &references[table] {
        if !visiting[target] {
            let below = layer(target, references, layers, visiting);
            deepest = Some(deepest.map_or(below, |current: usize| current.max(below)));
        }
    }
    visiting[table] = false;
    let value = deepest.map_or(0, |below| below + 1);
    layers[table] = Some(value);
    value
}

fn cell_number(value: usize) -> u32 {
    u32::try_from(value).unwrap_or(u32::MAX)
}

pub fn schema_graph_layout(graph: &ObjectGraph) -> Vec<GraphCell> {
    let count = graph.tables.len();
    let index: HashMap<&str, usize> = graph
        .tables
        .iter()
        .enumerate()
        .map(|(position, table)| (table.id.0.as_str(), position))
        .collect();
    let mut references = vec![Vec::new(); count];
    let mut neighbours = vec![Vec::new(); count];
    for edge in &graph.edges {
        let (Some(&source), Some(&target)) = (
            index.get(edge.source_id.0.as_str()),
            index.get(edge.target_id.0.as_str()),
        ) else {
            continue;
        };
        if source != target {
            references[source].push(target);
            neighbours[source].push(target);
            neighbours[target].push(source);
        }
    }
    let name = |table: &usize| graph.tables[*table].qualified_name.as_str();
    let mut by_name: Vec<usize> = (0..count).collect();
    by_name.sort_by_key(name);
    let mut seen = vec![false; count];
    let mut layers = vec![None; count];
    let mut visiting = vec![false; count];
    let mut blocks = Vec::new();
    for &start in &by_name {
        if seen[start] {
            continue;
        }
        seen[start] = true;
        let mut members = vec![start];
        let mut next = 0;
        while next < members.len() {
            for &neighbour in &neighbours[members[next]] {
                if !seen[neighbour] {
                    seen[neighbour] = true;
                    members.push(neighbour);
                }
            }
            next += 1;
        }
        members.sort_by_key(name);
        let mut block_layers: Vec<Vec<usize>> = Vec::new();
        for &member in &members {
            let depth = layer(member, &references, &mut layers, &mut visiting);
            if block_layers.len() <= depth {
                block_layers.resize(depth + 1, Vec::new());
            }
            block_layers[depth].push(member);
        }
        blocks.push(Block {
            members,
            layers: block_layers,
        });
    }
    blocks.sort_by(|a, b| {
        b.members
            .len()
            .cmp(&a.members.len())
            .then_with(|| name(&a.members[0]).cmp(name(&b.members[0])))
    });
    let widest = blocks
        .iter()
        .map(|block| block.layers.len())
        .max()
        .unwrap_or(0);
    let shelf_width = widest.max((count as f64).sqrt().ceil() as usize);
    let mut cells = vec![
        GraphCell {
            column: 0,
            row: 0,
            group: None,
        };
        count
    ];
    let (mut x, mut y, mut shelf_height, mut groups) = (0usize, 0usize, 0usize, 0u32);
    for block in &blocks {
        let width = block.layers.len();
        let height = block.layers.iter().map(Vec::len).max().unwrap_or(0);
        if x > 0 && x + width > shelf_width {
            y += shelf_height;
            x = 0;
            shelf_height = 0;
        }
        let group = (block.members.len() > 1).then(|| {
            groups += 1;
            groups - 1
        });
        for (column, members) in block.layers.iter().enumerate() {
            for (row, &member) in members.iter().enumerate() {
                cells[member] = GraphCell {
                    column: cell_number(x + column),
                    row: cell_number(y + row),
                    group,
                };
            }
        }
        x += width;
        shelf_height = shelf_height.max(height);
    }
    cells
}
