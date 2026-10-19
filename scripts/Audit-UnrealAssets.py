"""Read-only Asset Registry audit, run with Unreal's PythonScript commandlet.

No packages are loaded, saved, renamed or deleted. Packages outside the
dependency closure are review candidates only: dynamic loads can be invisible.
"""
import json
import os
import re
from pathlib import Path
from collections import deque

import unreal


def main():
    project = Path(unreal.Paths.project_dir()).resolve()
    output = Path(os.environ.get('DEVICELINK_ASSET_AUDIT_OUTPUT',
                                str(project.parent / 'artifacts' / 'asset-audit')))
    seed = os.environ.get('DEVICELINK_ASSET_AUDIT_MAP', '/Game/Maps/DeviceLinkLab')
    if not seed.startswith('/Game/') or '..' in seed:
        raise ValueError('A /Game/ package name is required')
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(synchronous_search=True)
    assets = registry.get_assets_by_path('/Game', recursive=True)
    packages = {str(asset.package_name) for asset in assets}
    if seed not in packages:
        raise ValueError('Map package not found: ' + seed)
    relative = seed.removeprefix('/Game/')
    external_roots = ['/Game/__ExternalActors__/' + relative + '/',
                      '/Game/__ExternalObjects__/' + relative + '/']
    # World Partition packages may not have conventional dependency edges.
    seeds = {seed} | {p for p in packages if any(p.startswith(r) for r in external_roots)}
    source_references = {}
    for source in (project / 'Source').rglob('*'):
        if source.suffix.lower() not in {'.cpp', '.h'}:
            continue
        for reference in re.findall(r'"(/Game/[A-Za-z0-9_/.]+)"', source.read_text(encoding='utf-8-sig')):
            package = reference.split('.')[0]
            source_references.setdefault(package, []).append(source.relative_to(project).as_posix())
            seeds.add(package)
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True,
        include_searchable_names=True, include_soft_management_references=True,
        include_hard_management_references=True)
    pending = deque(sorted(seeds))
    visited = set()
    edges = {}
    while pending:
        package = pending.popleft()
        if package in visited:
            continue
        visited.add(package)
        dependencies = sorted({str(p) for p in registry.get_dependencies(package, options)
                               if str(p).startswith('/Game/')})
        edges[package] = dependencies
        pending.extend(p for p in dependencies if p not in visited)
    # Enumerate files as well, including external packages absent from registry.
    rows = []
    for path in sorted((project / 'Content').rglob('*')):
        if path.is_file() and path.suffix.lower() in {'.uasset', '.umap'}:
            package = '/Game/' + path.relative_to(project / 'Content').with_suffix('').as_posix()
            rows.append({'package': package, 'bytes': path.stat().st_size,
                         'reachable': package in visited, 'in_registry': package in packages})
    groups = {}
    for row in rows:
        group = row['package'].split('/')[2]
        totals = groups.setdefault(group, {'bytes': 0, 'reachable_bytes': 0, 'files': 0})
        totals['bytes'] += row['bytes']
        totals['reachable_bytes'] += row['bytes'] if row['reachable'] else 0
        totals['files'] += 1
    report = {'map': seed, 'seeds': sorted(seeds), 'packages': rows,
              'source_references': source_references,
              'dependencies': edges, 'groups': groups,
              'missing_packages': sorted(visited - packages),
              'limitation': 'Not reached does not mean safe to delete; check dynamic loads and cook.'}
    output.mkdir(parents=True, exist_ok=True)
    (output / 'asset-references.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    lines = ['# Unreal asset reference audit', '', 'Map: `' + seed + '`', '',
             '| Folder | Files | MiB | Reachable MiB |', '| --- | ---: | ---: | ---: |']
    for group, counts in sorted(groups.items(), key=lambda item: -item[1]['bytes']):
        lines.append(f"| {group} | {counts['files']} | {counts['bytes']/1048576:.2f} | "
                     f"{counts['reachable_bytes']/1048576:.2f} |")
    lines += ['', 'Missing registry packages: ' + str(len(report['missing_packages'])), '',
              *['- `' + package + '`' for package in report['missing_packages']], '',
              report['limitation'], 'No source assets were modified.']
    (output / 'asset-references.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    unreal.log('DeviceLink asset audit written to ' + str(output))


if __name__ == '__main__':
    main()
