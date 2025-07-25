// Copyright Epic Games, Inc. All Rights Reserved.

class Info {
    constructor(id, tooltip, name, sources) {
        this.id = id;
        this.tooltip = tooltip;
        this.name = name;
        this.sources = sources;
        this.forcedEdges = new Set();
        this.defaultDests = new Set();
        this.blockAssetDests = new Set();
        this.importance = 0;
    }
    update(branch, getId) {
        const edgeId = (alias) => branch.def.name + '->' + getId(alias);
        // shuld really do the same for default and block assets
        this.forcedEdges = new Set([...this.forcedEdges, ...branch.def.forceFlowTo.map(edgeId)]);
        this.defaultDests = new Set([...this.defaultDests, ...branch.def.defaultFlow.map(edgeId)]);
        this.blockAssetDests = new Set([...this.blockAssetDests, ...branch.def.blockAssetTargets.map(edgeId)]);
    }
}

function decoratedAlias(bot, alias) {
    return bot + ':' + alias;
}
function aliasForBranch(branch) {
    return decoratedAlias(branch.bot, branch.def.upperName);
}

const EDGE_STYLES = {
    roboshelf: [['color', 'purple'], ['arrowhead', 'diamond']],
    forced: [],
    gated: [['color', 'darkorange']],
    defaultFlow: [['color', 'blue']],
    onRequest: [['color', 'darkgray'], ['style', 'dashed']],
    blockAssets: [['color', 'darkgray'], ['style', 'dashed'], ['arrowhead', 'odiamond']]
};

class Graph {
    constructor(allBranches, options) {
        this.allBranches = allBranches;
        this.options = options;
        this.nodeLabels = new Map();
        this.links = [];
        this.aliases = new Map();
        this.allInfos = [];
        this.linkOutward = new Set();
        this.linkInward = new Set();
        this.connectedNodes = new Set();
        this.branchList = [];
    }
    
    singleBot(botName) {
        this.branchList = this.allBranches.filter(b => b.bot === botName);
        this.addNodesAndEdges(false);
        const title = this.options.aliases.length > 0 ? botName + ' (' + this.options.aliases.join(", ") + ')': botName
        return this.makeGraph(title, botName + ' integration paths');
    }
    
    allBots() {
        this.branchList = this.options.botsToShow
            ? this.allBranches.filter(b => this.options.botsToShow.indexOf(b.bot) >= 0)
            : this.allBranches;
        this.findSharedNodes();
        this.addNodesAndEdges(!this.options.noGroups);

        let title = 'All bots';
        if (this.options.botsToShow)
        {
            title = `Bots: ${this.options.botsToShow.join(',')}`;
        }

        return this.makeGraph(title, 'Flow including shared bots');
    }

    addNodesAndEdges(grouped) {
        for (const branchStatus of this.branchList) {
            const botTitle = grouped
                                ? this.options.aliases && this.options.aliases.get(branchStatus.bot).length > 0
                                    ? branchStatus.bot + ' (' + this.options.aliases.get(branchStatus.bot).join(", ") + ')'
                                    : branchStatus.bot
                                : null;

            this.addBranch(branchStatus, grouped ? branchStatus.bot : null, botTitle);
        }
        for (const info of this.allInfos) {
            for (const sourceBranch of info.sources) {
                info.update(sourceBranch, this.getIdForBot(sourceBranch.bot));
            }
        }
        for (const branchStatus of this.branchList) {
            for (const flow of branchStatus.def.flowsTo) {
                this.addEdge(branchStatus, flow);
            }
        }
    }
    makeGraph(title, tooltip) {
        const lines = [
            'digraph robomerge {',
            'fontname="sans-serif"; labelloc=top; fontsize=16;',
            'edge [penwidth=2]; nodesep=.7; ranksep=1.2;',
            `label = "${title}";`,
            `tooltip="${tooltip}";`,
            `node [shape=box, style=filled, fontname="sans-serif", fillcolor=moccasin];`,
        ];
        const nodeGroups = [];
        for (const [groupName, v] of this.nodeLabels) {
            const nodeInfo = [];
            nodeGroups.push([groupName, nodeInfo]);
            for (const info of v) {
                const { id, tooltip, name, importance } = info;
                let factor = (Math.min(importance, 10) - 1) / 9;
                nodeInfo.push({
                    importance: factor,
                    marginX: (.2 * (1 - factor) + .4 * factor).toPrecision(1),
                    marginY: (.1 * (1 - factor) + .25 * factor).toPrecision(1),
                    fontSize: (14 * (1 - factor) + 20 * factor).toPrecision(2),
                    info: info
                });
            }
        }
        for (const [groupName, nodeInfo] of nodeGroups) {
            if (groupName !== 'nogroup') {
                lines.push(`subgraph cluster_${groupName} {
                            label="${nodeInfo[0].info.title}";
                        `);
            }
            for (const info of nodeInfo) {
                if (this.options.hideDisconnected && !this.connectedNodes.has(info.info.id)) {
                    continue;
                }
                const attrs = [
                    ['label', `"${info.info.name}"`],
                    ['tooltip', `"${info.info.tooltip}"`],
                    ['margin', `"${info.marginX},${info.marginY}"`],
                    ['fontsize', info.fontSize],
                ];
                if (info.importance > .5) {
                    attrs.push(['style', '"filled,bold"']);
                }
                if (info.info.graphNodeColor) {
                    attrs.push(['fillcolor', `"${info.info.graphNodeColor}"`]);
                }
                const attrStrs = attrs.map(([key, value]) => `${key}=${value}`);
                lines.push(`${info.info.id} [${attrStrs.join(', ')}];`);
            }
            if (groupName !== 'nogroup') {
                lines.push('}');
            }
        }
        for (const link of this.links) {
            // when there's forced/unforced pair, only the former is a constrait
            // (this makes flow )
            const combo = link.dst + '->' + link.src;
            if (combo && this.linkOutward.has(combo) && this.linkInward.has(combo)) {
                link.styles.push(['constraint', 'false']);
            }
            const styleStrs = link.styles.map(([key, value]) => `${key}=${value}`);
            const suffix = styleStrs.length === 0 ? '' : ` [${styleStrs.join(', ')}]`;
            lines.push(`${link.src} -> ${link.dst}${suffix} [tooltip="${link.tooltip}"];`);
        }
        lines.push('}');
        return lines;
    }
    addBranch(branchStatus, group = null, title = null) {
        const branch = branchStatus.def;
        if (this.aliases.has(decoratedAlias(branchStatus.bot, branch.upperName)))
            return;
        let tooltip = `${branchStatus.bot} - ${branch.rootPath}`;
        if (branch.aliases && branch.aliases.length !== 0) {
            tooltip += ` (${branch.aliases.join(', ')})`;
        }
        const info = new Info(`_${branchStatus.bot}_${branch.upperName.replace(/[^\w]+/g, '_')}`, tooltip, branch.name, [branchStatus]);
        info.title = title
        this.allInfos.push(info);
        if (branch.config.graphNodeColor) {
            info.graphNodeColor = branch.config.graphNodeColor;
        }
        if (branch.upperName === 'MAIN') {
            // special case branches named Main
            info.importance = 10;
        }
        // note: branch.upperName is always in branch.aliases
        for (const alias of branch.aliases) {
            this.aliases.set(decoratedAlias(branchStatus.bot, alias), info);
        }
        setDefault(this.nodeLabels, group || 'nogroup', []).push(info);
    }
    // dstName 
    addEdge(srcBranchStatus, dst) {
        const src = aliasForBranch(srcBranchStatus);
        const srcInfo = this.aliases.get(src);
        const dstInfo = this.aliases.get(decoratedAlias(srcBranchStatus.bot, dst));
        ++srcInfo.importance;
        ++dstInfo.importance;
        const hasEdge = (edges, target) => edges.has(srcBranchStatus.def.name + '->' + target.id);
        const isForced = hasEdge(srcInfo.forcedEdges, dstInfo);
        if (!isForced && this.options.showOnlyForced) {
            return;
        }
        const isGated = new Map(srcBranchStatus.def.edgeProperties).get(dst).lastGoodCLPath
        this.connectedNodes.add(srcInfo.id);
        this.connectedNodes.add(dstInfo.id);
        const edgeStyle = isForced ? (isGated ? 'gated' : 'forced') :
                hasEdge(srcInfo.defaultDests, dstInfo) ? 'defaultFlow' :
                    hasEdge(srcInfo.blockAssetDests, dstInfo) ? 'blockAssets' : 'onRequest';
        const styles = [...EDGE_STYLES[edgeStyle]];

        // prepare the tool tip of the edge to be as informative as possible
        let suffix = '';
        if(edgeStyle !== 'defaultFlow'){
            suffix = `(${edgeStyle})`;
        }

        let tooltipcontent = `${srcInfo.name} -> ${dstInfo.name} ${suffix}\n`;
        tooltipcontent += `\n${srcInfo.name}:\n${srcInfo.tooltip}\n`;
        tooltipcontent += `\n${dstInfo.name}:\n${dstInfo.tooltip}\n`;
        if (isGated)
        {
            tooltipcontent += `\nlast good CL path:\n${isGated}`
        }

        const link = { src: srcInfo.id, dst: dstInfo.id, styles, tooltip: tooltipcontent };
        this.links.push(link);
        if (isForced) {
            this.linkOutward.add(link.src + '->' + link.dst);
        }
        else if (edgeStyle === 'blockAssets' || edgeStyle === 'onRequest') {
            this.linkInward.add(link.dst + '->' + link.src);
        }
    }
    findSharedNodes() {
        const streamMap = new Map();
        for (const branchStatus of this.branchList) {
            let key = branchStatus.def.rootPath;
            if(branchStatus.def.uniqueBranch)
            {
                key = branchStatus.def.name;
            }

            const [color, streams] = setDefault(streamMap, key, [null, []]);
            streams.push(branchStatus);
            const nodeColor = branchStatus.def.config.graphNodeColor;
            if (!color && nodeColor) {
                streamMap.set(key, [nodeColor, streams]);
            }
        }

        // create a shared info for each set of nodes monitoring the same stream
        for (const [k, v] of streamMap) {
            const [color, streams] = v;
            if (streams.length > 1) {

                // create a tooltip with information from all the branches
                let globalToolTip = '';
                for (const branchStatus of streams) {
                    let tooltip = `${branchStatus.bot} - ${branchStatus.def.rootPath}`;
                    if (branchStatus.def.aliases && branchStatus.def.aliases.length !== 0) {
                        tooltip += ` (${branchStatus.def.aliases.join(', ')})`;
                    }
                    globalToolTip += tooltip + "\n";
                }
                globalToolTip = globalToolTip.trim();

                const info = new Info(k.replace(/[^\w]+/g, '_'), globalToolTip, k, streams);
                this.allInfos.push(info);
                if (color) {
                    info.graphNodeColor = color;
                }
                for (const branchStatus of streams) {
                    for (const alias of branchStatus.def.aliases) {
                        this.aliases.set(decoratedAlias(branchStatus.bot, alias), info);
                    }
                }
                setDefault(this.nodeLabels, 'nogroup', []).push(info);
            }
        }
    }
    getIdForBot(bot) {
        return (alias) => {
            const info = this.aliases.get(decoratedAlias(bot, alias));
            return info ? info.id : alias;
        };
    }
}