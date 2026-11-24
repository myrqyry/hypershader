/**
 * HyperShaderFX Visual Builder - Graph Engine
 * Core data model for node graph with serialization
 */

class ShaderGraph {
    constructor() {
        this.nodes = new Map();
        this.edges = [];
        this.nextNodeId = 1;
        this.selectedNodes = new Set();
        this.listeners = new Map();
    }

    // Event system
    on(event, callback) {
        if (!this.listeners.has(event)) {
            this.listeners.set(event, []);
        }
        this.listeners.get(event).push(callback);
    }

    emit(event, data) {
        const callbacks = this.listeners.get(event) || [];
        callbacks.forEach(cb => cb(data));
    }

    // Node management
    addNode(type, position, catalog) {
        const nodeDef = catalog.findNode(type);
        if (!nodeDef) {
            console.error(`Unknown node type: ${type}`);
            return null;
        }

        const id = `node_${this.nextNodeId++}`;
        const node = {
            id,
            type,
            position: { ...position },
            definition: nodeDef,
            values: {},
            collapsed: false
        };

        // Initialize default values
        if (nodeDef.inputs) {
            nodeDef.inputs.forEach(input => {
                if (input.default !== undefined) {
                    node.values[input.name] = input.default;
                }
            });
        }

        this.nodes.set(id, node);
        this.emit('nodeAdded', node);
        return node;
    }

    removeNode(nodeId) {
        const node = this.nodes.get(nodeId);
        if (!node) return;

        // Remove connected edges
        this.edges = this.edges.filter(edge => {
            if (edge.from.nodeId === nodeId || edge.to.nodeId === nodeId) {
                this.emit('edgeRemoved', edge);
                return false;
            }
            return true;
        });

        this.nodes.delete(nodeId);
        this.selectedNodes.delete(nodeId);
        this.emit('nodeRemoved', node);
    }

    getNode(nodeId) {
        return this.nodes.get(nodeId);
    }

    moveNode(nodeId, position) {
        const node = this.nodes.get(nodeId);
        if (node) {
            node.position = { ...position };
            this.emit('nodeMoved', node);
        }
    }

    setNodeValue(nodeId, portName, value) {
        const node = this.nodes.get(nodeId);
        if (node) {
            node.values[portName] = value;
            this.emit('nodeValueChanged', { node, portName, value });
        }
    }

    // Selection
    selectNode(nodeId, additive = false) {
        if (!additive) {
            this.selectedNodes.clear();
        }
        this.selectedNodes.add(nodeId);
        this.emit('selectionChanged', this.selectedNodes);
    }

    deselectAll() {
        this.selectedNodes.clear();
        this.emit('selectionChanged', this.selectedNodes);
    }

    // Edge management
    canConnect(fromNodeId, fromPort, toNodeId, toPort) {
        const fromNode = this.nodes.get(fromNodeId);
        const toNode = this.nodes.get(toNodeId);

        if (!fromNode || !toNode) return false;
        if (fromNodeId === toNodeId) return false;

        // Find port definitions
        const fromPortDef = fromNode.definition.outputs?.find(p => p.name === fromPort);
        const toPortDef = toNode.definition.inputs?.find(p => p.name === toPort);

        if (!fromPortDef || !toPortDef) return false;

        // Type compatibility check
        return this.typesCompatible(fromPortDef.type, toPortDef.type);
    }

    typesCompatible(fromType, toType) {
        if (fromType === toType) return true;
        
        // Allow implicit conversions
        const numericTypes = ['float', 'float2', 'float3', 'float4', 'int'];
        if (numericTypes.includes(fromType) && numericTypes.includes(toType)) {
            return true;
        }
        
        return false;
    }

    connect(fromNodeId, fromPort, toNodeId, toPort) {
        if (!this.canConnect(fromNodeId, fromPort, toNodeId, toPort)) {
            return false;
        }

        // Remove existing connection to this input port
        this.edges = this.edges.filter(edge => {
            if (edge.to.nodeId === toNodeId && edge.to.port === toPort) {
                this.emit('edgeRemoved', edge);
                return false;
            }
            return true;
        });

        const edge = {
            id: `edge_${Date.now()}`,
            from: { nodeId: fromNodeId, port: fromPort },
            to: { nodeId: toNodeId, port: toPort }
        };

        this.edges.push(edge);
        this.emit('edgeAdded', edge);
        return true;
    }

    disconnect(edgeId) {
        const index = this.edges.findIndex(e => e.id === edgeId);
        if (index !== -1) {
            const edge = this.edges.splice(index, 1)[0];
            this.emit('edgeRemoved', edge);
        }
    }

    getInputConnection(nodeId, portName) {
        return this.edges.find(e => e.to.nodeId === nodeId && e.to.port === portName);
    }

    getOutputConnections(nodeId, portName) {
        return this.edges.filter(e => e.from.nodeId === nodeId && e.from.port === portName);
    }

    // Topological sort for code generation
    topologicalSort() {
        const visited = new Set();
        const sorted = [];
        const visiting = new Set();

        const visit = (nodeId) => {
            if (visited.has(nodeId)) return true;
            if (visiting.has(nodeId)) return false; // Cycle detected

            visiting.add(nodeId);

            // Visit dependencies (nodes connected to inputs)
            const node = this.nodes.get(nodeId);
            if (node && node.definition.inputs) {
                for (const input of node.definition.inputs) {
                    const edge = this.getInputConnection(nodeId, input.name);
                    if (edge) {
                        if (!visit(edge.from.nodeId)) {
                            console.error('Cycle detected in graph');
                            return false;
                        }
                    }
                }
            }

            visiting.delete(nodeId);
            visited.add(nodeId);
            sorted.push(nodeId);
            return true;
        };

        // Find output nodes and start from there
        for (const [nodeId, node] of this.nodes) {
            if (node.definition.category === 'Output') {
                visit(nodeId);
            }
        }

        // Visit any unconnected nodes
        for (const nodeId of this.nodes.keys()) {
            if (!visited.has(nodeId)) {
                visit(nodeId);
            }
        }

        return sorted;
    }

    // Find output node
    getOutputNode() {
        for (const [nodeId, node] of this.nodes) {
            if (node.type === 'output_color') {
                return node;
            }
        }
        return null;
    }

    // Serialization
    serialize() {
        return {
            version: 1,
            nodes: Array.from(this.nodes.values()).map(node => ({
                id: node.id,
                type: node.type,
                position: node.position,
                values: node.values
            })),
            edges: this.edges.map(edge => ({
                from: edge.from,
                to: edge.to
            }))
        };
    }

    deserialize(data, catalog) {
        this.clear();

        if (data.version !== 1) {
            console.warn('Unknown graph version:', data.version);
        }

        // Restore nodes
        let maxId = 0;
        for (const nodeData of data.nodes) {
            const nodeDef = catalog.findNode(nodeData.type);
            if (!nodeDef) {
                console.warn(`Unknown node type during load: ${nodeData.type}`);
                continue;
            }

            const node = {
                id: nodeData.id,
                type: nodeData.type,
                position: nodeData.position,
                definition: nodeDef,
                values: nodeData.values || {},
                collapsed: false
            };

            this.nodes.set(node.id, node);
            this.emit('nodeAdded', node);

            // Track max ID
            const idNum = parseInt(nodeData.id.replace('node_', ''));
            if (!isNaN(idNum) && idNum > maxId) {
                maxId = idNum;
            }
        }

        this.nextNodeId = maxId + 1;

        // Restore edges
        for (const edgeData of data.edges) {
            const edge = {
                id: `edge_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`,
                from: edgeData.from,
                to: edgeData.to
            };
            this.edges.push(edge);
            this.emit('edgeAdded', edge);
        }
    }

    clear() {
        for (const nodeId of this.nodes.keys()) {
            this.emit('nodeRemoved', this.nodes.get(nodeId));
        }
        for (const edge of this.edges) {
            this.emit('edgeRemoved', edge);
        }
        this.nodes.clear();
        this.edges = [];
        this.selectedNodes.clear();
        this.nextNodeId = 1;
    }
}

// Export for use
window.ShaderGraph = ShaderGraph;
