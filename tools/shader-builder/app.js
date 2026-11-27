/**
 * hypershader Visual Builder - Main Application
 * Ties together UI, graph engine, and code generation
 */

class ShaderBuilderApp {
    constructor() {
        this.catalog = new NodeCatalog(NODE_CATALOG);
        this.graph = new ShaderGraph();
        this.codegen = new ShaderCodeGenerator();
        this.passes = [];
        this.activePass = '';
        this.finalPassGraph = {version: 1, nodes: [], edges: []};
        this.switchingPass = false;
        this.recoveryDraftId = null;
        this.recoverySaveTimer = null;
        
        // Canvas state
        this.canvas = {
            scale: 1,
            offsetX: 0,
            offsetY: 0,
            isDragging: false,
            dragStart: { x: 0, y: 0 },
            selectedNode: null
        };

        // Connection dragging state
        this.connecting = {
            active: false,
            fromNode: null,
            fromPort: null,
            isOutput: false,
            tempLine: null
        };

        // Init
        this.init();
    }

    init() {
        this.cacheElements();
        this.previewRenderer = null;
        this.previewError = null;
        try {
            this.previewRenderer = new ShaderPreviewRenderer(this.elements.previewCanvas);
        } catch (error) {
            this.previewError = error.message;
        }
        this.setupEventListeners();
        this.renderNodePalette();
        this.setupGraphListeners();
        this.refreshPassControls();
        
        // Initialize AI Pilot
        if (window.ShaderAIPilot) {
            this.aiPilot = new ShaderAIPilot(this);
        }

        this.updateCode();

        const drawPreview = (timestamp) => {
            if (this.previewRenderer && this.previewRenderer.program) {
                try {
                    this.previewRenderer.renderFrame(timestamp);
                } catch (error) {
                    this.previewRenderer.clear();
                    this.showPreviewMessage(error.message);
                }
            }
            this.previewFrame = requestAnimationFrame(drawPreview);
        };
        this.previewFrame = requestAnimationFrame(drawPreview);
        window.addEventListener('pagehide', () => {
            cancelAnimationFrame(this.previewFrame);
            if (this.previewRenderer) this.previewRenderer.dispose();
        }, { once: true });
    }

    scheduleRecoverySave() {
        if (!window.shaderBridge) return;
        if (!this.recoveryDraftId) {
            this.recoveryDraftId = (crypto.randomUUID && crypto.randomUUID()) || `draft_${Date.now()}`;
        }

        if (this.recoverySaveTimer) {
            clearTimeout(this.recoverySaveTimer);
        }

        this.recoverySaveTimer = setTimeout(() => {
            const payload = JSON.stringify({
                name: this.elements.shaderName.value,
                ...this.serializeProject()
            });
            window.shaderBridge.saveRecoveredDraft(this.recoveryDraftId, payload);
            this.recoverySaveTimer = null;
        }, 250);
    }

    clearRecoveryDraft() {
        if (!window.shaderBridge || !this.recoveryDraftId) return;
        if (this.recoverySaveTimer) {
            clearTimeout(this.recoverySaveTimer);
            this.recoverySaveTimer = null;
        }
        window.shaderBridge.deleteRecoveredDraft(this.recoveryDraftId);
        this.recoveryDraftId = null;
    }

    cacheElements() {
        this.elements = {
            canvasContainer: document.getElementById('canvasContainer'),
            nodesLayer: document.getElementById('nodesLayer'),
            connectionsLayer: document.getElementById('connectionsLayer'),
            nodeCategories: document.getElementById('nodeCategories'),
            nodeSearch: document.getElementById('nodeSearch'),
            propertiesContent: document.getElementById('propertiesContent'),
            codePreview: document.getElementById('codePreview'),
            previewCanvas: document.getElementById('previewCanvas'),
            previewOverlay: document.getElementById('previewOverlay'),
            contextMenu: document.getElementById('contextMenu'),
            toastContainer: document.getElementById('toastContainer'),
            zoomLevel: document.getElementById('zoomLevel'),
            shaderName: document.getElementById('shaderName'),
            aiInput: document.getElementById('aiPromptInput'),
            btnRunAI: document.getElementById('btnRunAI')
        };
    }

    setupEventListeners() {
        const byId = id => document.getElementById(id);
        byId('btnNew').addEventListener('click', () => this.newGraph());
        byId('btnLoad').addEventListener('click', () => this.loadGraph());
        byId('btnSave').addEventListener('click', () => this.saveGraph());
        byId('btnExport').addEventListener('click', () => this.exportShader());
        byId('btnCopyCode').addEventListener('click', () => this.copyCode());
        byId('fileInput').addEventListener('change', e => this.handleFileLoad(e));
        byId('btnAddPass').addEventListener('click', () => this.addPass());
        byId('btnRemovePass').addEventListener('click', () => this.removePass());
        byId('btnPassUp').addEventListener('click', () => this.movePass(-1));
        byId('btnPassDown').addEventListener('click', () => this.movePass(1));
        byId('passSelection').addEventListener('change', e => this.selectPass(e.target.value));
        byId('passDivisor').addEventListener('change', e => this.setPassDescriptor('divisor', Number(e.target.value)));
        byId('passFormat').addEventListener('change', e => this.setPassDescriptor('format', Number(e.target.value)));
        this.elements.canvasContainer.addEventListener('mousedown', e => this.onCanvasMouseDown(e));
        document.addEventListener('mousemove', e => this.onCanvasMouseMove(e));
        document.addEventListener('mouseup', e => this.onCanvasMouseUp(e));
        this.elements.canvasContainer.addEventListener('wheel', e => this.onCanvasWheel(e), {passive:false});
        byId('btnZoomIn').addEventListener('click', () => this.zoom(1.2));
        byId('btnZoomOut').addEventListener('click', () => this.zoom(0.8));
        byId('btnFitView').addEventListener('click', () => this.fitView());
        this.elements.nodeSearch.addEventListener('input', e => this.filterNodes(e.target.value));
        document.addEventListener('keydown', e => this.onKeyDown(e));
        const refreshPreview = document.getElementById('btnRefreshPreview');
        if (refreshPreview) refreshPreview.addEventListener('click', () => this.updatePreview());
        const curateButton = document.getElementById('btnCurate');
        if (curateButton) curateButton.addEventListener('click', () => this.curateGraph());

        // AI Pilot
        if (this.elements.btnRunAI) {
            this.elements.btnRunAI.addEventListener('click', () => this.runAIPilot());
            this.elements.aiInput.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') this.runAIPilot();
            });
        }
    }

    curateGraph() {
        if (this.graph.nodes.size && !confirm('Replace the selected graph with a new curated composition? Other passes are retained.')) return;
        try {
            const candidate = makeCuratedGraph(this.catalog);
            // Build a valid candidate before replacing any existing user work.
            this.codegen.generate(candidate.graph);
            this.codegen.buildPreviewShader(candidate.graph);
            if (!this.passes.length) this.clearRecoveryDraft();
            this.replaceCanvasGraph(candidate.graph.serialize());
            this.elements.shaderName.value = candidate.name;
            this.updateCode();
            this.fitView();
            this.showToast(candidate.name, 'success');
        } catch (error) {
            this.showToast('Curation failed: ' + error.message, 'error');
        }
    }

    runAIPilot() {
        const prompt = this.elements.aiInput.value.trim();
        if (!prompt || !this.aiPilot) return;
        
        this.aiPilot.processPrompt(prompt);
        this.elements.aiInput.value = '';
    }

    setupGraphListeners() {
        this.graph.on('nodeAdded', (node) => {
            if (!this.switchingPass && node.type.startsWith('input_pass_') && !node.values.passId)
                node.values.passId = this.activePass || this.passes[0]?.id || '';
            this.renderNode(node);
            this.updateCode();
        });
        this.graph.on('nodeRemoved', (node) => this.removeNodeElement(node));
        this.graph.on('nodeMoved', (node) => this.updateNodePosition(node));
        this.graph.on('edgeAdded', (edge) => {
            this.renderEdge(edge);
            this.updateCode();
        });
        this.graph.on('edgeRemoved', (edge) => {
            this.removeEdgeElement(edge);
            this.updateCode();
        });
        this.graph.on('selectionChanged', () => this.updateSelection());
        this.graph.on('nodeValueChanged', () => this.updateCode());
    }

    // ============================================
    // Node Palette
    // ============================================
    renderNodePalette() {
        const container = this.elements.nodeCategories;
        container.innerHTML = '';

        for (const category of this.catalog.getCategories()) {
            const catEl = document.createElement('div');
            catEl.className = 'category';
            catEl.innerHTML = `
                <button class="category-header">
                    <span class="category-icon">${category.icon}</span>
                    <span>${category.name}</span>
                    <span class="category-arrow">▶</span>
                </button>
                <div class="category-nodes"></div>
            `;

            const header = catEl.querySelector('.category-header');
            const nodesContainer = catEl.querySelector('.category-nodes');

            header.addEventListener('click', () => {
                catEl.classList.toggle('expanded');
            });

            for (const node of category.nodes) {
                if (node.persistentOnly && !this.passes.length) continue;
                const nodeEl = document.createElement('div');
                nodeEl.className = 'node-item';
                nodeEl.draggable = true;
                nodeEl.dataset.nodeType = node.id;
                nodeEl.innerHTML = `
                    <span class="node-item-color" style="background: ${category.color}"></span>
                    <span>${node.name}</span>
                `;

                nodeEl.addEventListener('dragstart', (e) => {
                    e.dataTransfer.setData('nodeType', node.id);
                    e.dataTransfer.effectAllowed = 'copy';
                });

                nodesContainer.appendChild(nodeEl);
            }

            container.appendChild(catEl);
        }

        // Expand first category by default
        container.querySelector('.category')?.classList.add('expanded');
    }

    filterNodes(query) {
        const categories = this.elements.nodeCategories.querySelectorAll('.category');
        const q = query.toLowerCase();

        categories.forEach(cat => {
            const nodes = cat.querySelectorAll('.node-item');
            let hasVisible = false;

            nodes.forEach(node => {
                const name = node.textContent.toLowerCase();
                const visible = !q || name.includes(q);
                node.style.display = visible ? '' : 'none';
                if (visible) hasVisible = true;
            });

            cat.style.display = hasVisible ? '' : 'none';
            if (q && hasVisible) cat.classList.add('expanded');
        });
    }

    // ============================================
    // Canvas Interaction
    // ============================================
    onCanvasMouseDown(e) {
        if (e.target === this.elements.canvasContainer || e.target === this.elements.nodesLayer) {
            if (e.button === 0) {
                // Left click - start panning
                this.canvas.isDragging = true;
                this.canvas.dragStart = { x: e.clientX, y: e.clientY };
                this.elements.canvasContainer.style.cursor = 'grabbing';
            }
            this.graph.deselectAll();
        }
    }

    onCanvasMouseMove(e) {
        if (this.canvas.isDragging) {
            const dx = e.clientX - this.canvas.dragStart.x;
            const dy = e.clientY - this.canvas.dragStart.y;
            this.canvas.offsetX += dx;
            this.canvas.offsetY += dy;
            this.canvas.dragStart = { x: e.clientX, y: e.clientY };
            this.updateCanvasTransform();
        }

        if (this.connecting.active) {
            this.updateTempConnection(e);
        }
    }

    onCanvasMouseUp(e) {
        this.canvas.isDragging = false;
        this.elements.canvasContainer.style.cursor = '';

        if (this.connecting.active) {
            this.endConnection(e);
        }
    }

    onCanvasWheel(e) {
        e.preventDefault();
        const delta = e.deltaY > 0 ? 0.9 : 1.1;
        this.zoom(delta, e.clientX, e.clientY);
    }

    zoom(factor, centerX, centerY) {
        const rect = this.elements.canvasContainer.getBoundingClientRect();
        const cx = centerX ?? rect.width / 2;
        const cy = centerY ?? rect.height / 2;

        const oldScale = this.canvas.scale;
        this.canvas.scale = Math.max(0.25, Math.min(2, this.canvas.scale * factor));
        
        // Adjust offset to zoom toward mouse
        const scaleChange = this.canvas.scale / oldScale;
        this.canvas.offsetX = cx - (cx - this.canvas.offsetX) * scaleChange;
        this.canvas.offsetY = cy - (cy - this.canvas.offsetY) * scaleChange;

        this.updateCanvasTransform();
    }

    updateCanvasTransform() {
        const transform = `translate(${this.canvas.offsetX}px, ${this.canvas.offsetY}px) scale(${this.canvas.scale})`;
        this.elements.nodesLayer.style.transform = transform;
        this.elements.connectionsLayer.style.transform = transform;
        this.elements.zoomLevel.textContent = `${Math.round(this.canvas.scale * 100)}%`;
        this.updateAllConnections();
    }

    fitView() {
        if (this.graph.nodes.size === 0) {
            this.canvas.scale = 1;
            this.canvas.offsetX = 0;
            this.canvas.offsetY = 0;
        } else {
            // Calculate bounding box of all nodes
            let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
            for (const node of this.graph.nodes.values()) {
                minX = Math.min(minX, node.position.x);
                minY = Math.min(minY, node.position.y);
                maxX = Math.max(maxX, node.position.x + 180);
                maxY = Math.max(maxY, node.position.y + 100);
            }

            const rect = this.elements.canvasContainer.getBoundingClientRect();
            const padding = 50;
            const scaleX = (rect.width - padding * 2) / (maxX - minX);
            const scaleY = (rect.height - padding * 2) / (maxY - minY);
            
            this.canvas.scale = Math.min(1, Math.min(scaleX, scaleY));
            this.canvas.offsetX = padding - minX * this.canvas.scale + (rect.width - (maxX - minX) * this.canvas.scale) / 2 - padding;
            this.canvas.offsetY = padding - minY * this.canvas.scale + (rect.height - (maxY - minY) * this.canvas.scale) / 2 - padding;
        }
        this.updateCanvasTransform();
    }

    // ============================================
    // Node Rendering
    // ============================================
    renderNode(node) {
        const el = document.createElement('div');
        el.className = 'node';
        el.id = `node-${node.id}`;
        el.style.left = `${node.position.x}px`;
        el.style.top = `${node.position.y}px`;

        const def = node.definition;
        el.setAttribute('data-category', def.categoryName);
        const color = def.categoryColor || '#6366f1';

        el.innerHTML = `
            <div class="node-header" style="background: linear-gradient(135deg, ${color}40, ${color}20)">
                <span class="node-icon">${this.getCategoryIcon(def.categoryName)}</span>
                <span class="node-title">${def.name}</span>
            </div>
            <div class="node-body">
                ${this.renderNodePorts(node)}
            </div>
        `;

        // Drag handling
        const header = el.querySelector('.node-header');
        header.addEventListener('mousedown', (e) => this.onNodeDragStart(e, node));

        // Port handling
        el.querySelectorAll('.port').forEach(port => {
            port.addEventListener('mousedown', (e) => this.onPortMouseDown(e, node, port));
        });

        // Selection
        el.addEventListener('click', (e) => {
            e.stopPropagation();
            this.graph.selectNode(node.id, e.shiftKey);
        });

        // Context menu
        el.addEventListener('contextmenu', (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.showContextMenu(e, node);
        });

        this.elements.nodesLayer.appendChild(el);
    }

    renderNodePorts(node) {
        const def = node.definition;
        let html = '';

        // Input ports
        for (const input of def.inputs || []) {
            const hasConnection = this.graph.getInputConnection(node.id, input.name);
            html += `
                <div class="port-row input">
                    <div class="port input ${hasConnection ? 'connected' : ''}" 
                         data-port="${input.name}" 
                         data-type="${input.type}"
                         data-direction="input"></div>
                    <span class="port-label">${input.label || input.name}</span>
                    ${!hasConnection && input.type === 'float' ? `
                        <input type="number" class="port-input" 
                               value="${node.values[input.name] ?? input.default ?? 0}"
                               data-port="${input.name}"
                               step="0.1">
                    ` : ''}
                </div>
            `;
        }

        // Output ports
        for (const output of def.outputs || []) {
            html += `
                <div class="port-row output">
                    <span class="port-label">${output.label || output.name}</span>
                    <div class="port output" 
                         data-port="${output.name}" 
                         data-type="${output.type}"
                         data-direction="output"></div>
                </div>
            `;
        }

        return html;
    }

    getCategoryIcon(categoryName) {
        const category = this.catalog.getCategories().find(c => c.name === categoryName);
        return category?.icon || '📦';
    }

    removeNodeElement(node) {
        const el = document.getElementById(`node-${node.id}`);
        if (el) el.remove();
        this.updateCode();
    }

    updateNodePosition(node) {
        const el = document.getElementById(`node-${node.id}`);
        if (el) {
            el.style.left = `${node.position.x}px`;
            el.style.top = `${node.position.y}px`;
        }
        this.updateAllConnections();
    }

    updateSelection() {
        document.querySelectorAll('.node').forEach(el => {
            const nodeId = el.id.replace('node-', '');
            el.classList.toggle('selected', this.graph.selectedNodes.has(nodeId));
        });

        // Update properties panel
        if (this.graph.selectedNodes.size === 1) {
            const nodeId = Array.from(this.graph.selectedNodes)[0];
            this.showNodeProperties(this.graph.getNode(nodeId));
        } else {
            this.elements.propertiesContent.innerHTML = '<p class="placeholder-text">Select a node to edit properties</p>';
        }
    }

    // ============================================
    // Node Dragging
    // ============================================
    onNodeDragStart(e, node) {
        e.stopPropagation();
        const startX = e.clientX;
        const startY = e.clientY;
        const startPos = { ...node.position };

        const onMove = (e) => {
            const dx = (e.clientX - startX) / this.canvas.scale;
            const dy = (e.clientY - startY) / this.canvas.scale;
            this.graph.moveNode(node.id, {
                x: startPos.x + dx,
                y: startPos.y + dy
            });
        };

        const onUp = () => {
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
        };

        document.addEventListener('mousemove', onMove);
        document.addEventListener('mouseup', onUp);
    }

    // ============================================
    // Connections
    // ============================================
    onPortMouseDown(e, node, portEl) {
        e.stopPropagation();
        e.preventDefault();

        const portName = portEl.dataset.port;
        const isOutput = portEl.dataset.direction === 'output';

        this.connecting = {
            active: true,
            fromNode: node.id,
            fromPort: portName,
            isOutput: isOutput,
            tempLine: null
        };

        // Create temporary line
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        line.classList.add('connection', 'dragging');
        this.elements.connectionsLayer.appendChild(line);
        this.connecting.tempLine = line;
    }

    updateTempConnection(e) {
        if (!this.connecting.tempLine) return;

        const rect = this.elements.canvasContainer.getBoundingClientRect();
        const endX = (e.clientX - rect.left - this.canvas.offsetX) / this.canvas.scale;
        const endY = (e.clientY - rect.top - this.canvas.offsetY) / this.canvas.scale;

        const fromEl = document.querySelector(`#node-${this.connecting.fromNode} .port[data-port="${this.connecting.fromPort}"]`);
        if (!fromEl) return;

        const fromRect = fromEl.getBoundingClientRect();
        const startX = (fromRect.left + fromRect.width / 2 - rect.left - this.canvas.offsetX) / this.canvas.scale;
        const startY = (fromRect.top + fromRect.height / 2 - rect.top - this.canvas.offsetY) / this.canvas.scale;

        const path = this.connecting.isOutput
            ? this.createBezierPath(startX, startY, endX, endY)
            : this.createBezierPath(endX, endY, startX, startY);

        this.connecting.tempLine.setAttribute('d', path);
    }

    endConnection(e) {
        if (!this.connecting.active) return;

        // Find port under mouse
        const target = document.elementFromPoint(e.clientX, e.clientY);
        const portEl = target?.closest('.port');

        if (portEl) {
            const nodeEl = portEl.closest('.node');
            const toNodeId = nodeEl.id.replace('node-', '');
            const toPort = portEl.dataset.port;
            const toIsOutput = portEl.dataset.direction === 'output';

            // Connect if valid (output -> input)
            if (this.connecting.isOutput && !toIsOutput) {
                this.graph.connect(this.connecting.fromNode, this.connecting.fromPort, toNodeId, toPort);
            } else if (!this.connecting.isOutput && toIsOutput) {
                this.graph.connect(toNodeId, toPort, this.connecting.fromNode, this.connecting.fromPort);
            }
        }

        // Cleanup
        if (this.connecting.tempLine) {
            this.connecting.tempLine.remove();
        }
        this.connecting = { active: false, fromNode: null, fromPort: null, isOutput: false, tempLine: null };
        this.updateCode();
    }

    renderEdge(edge) {
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        path.id = `edge-${edge.id}`;
        path.classList.add('connection');
        
        // Get type for color
        const fromNode = this.graph.getNode(edge.from.nodeId);
        const portDef = fromNode?.definition.outputs?.find(p => p.name === edge.from.port);
        if (portDef) {
            path.classList.add(portDef.type);
        }

        this.elements.connectionsLayer.appendChild(path);
        this.updateEdgePosition(edge);
        
        // Update port connected state
        this.updatePortStates();
    }

    removeEdgeElement(edge) {
        const el = document.getElementById(`edge-${edge.id}`);
        if (el) el.remove();
        this.updatePortStates();
    }

    updateEdgePosition(edge) {
        const path = document.getElementById(`edge-${edge.id}`);
        if (!path) return;

        const fromPort = document.querySelector(`#node-${edge.from.nodeId} .port[data-port="${edge.from.port}"]`);
        const toPort = document.querySelector(`#node-${edge.to.nodeId} .port[data-port="${edge.to.port}"]`);

        if (!fromPort || !toPort) return;

        const containerRect = this.elements.canvasContainer.getBoundingClientRect();
        
        const fromRect = fromPort.getBoundingClientRect();
        const toRect = toPort.getBoundingClientRect();

        const x1 = (fromRect.left + fromRect.width / 2 - containerRect.left - this.canvas.offsetX) / this.canvas.scale;
        const y1 = (fromRect.top + fromRect.height / 2 - containerRect.top - this.canvas.offsetY) / this.canvas.scale;
        const x2 = (toRect.left + toRect.width / 2 - containerRect.left - this.canvas.offsetX) / this.canvas.scale;
        const y2 = (toRect.top + toRect.height / 2 - containerRect.top - this.canvas.offsetY) / this.canvas.scale;

        path.setAttribute('d', this.createBezierPath(x1, y1, x2, y2));
    }

    updateAllConnections() {
        for (const edge of this.graph.edges) {
            this.updateEdgePosition(edge);
        }
    }

    updatePortStates() {
        document.querySelectorAll('.port.input').forEach(port => {
            const nodeId = port.closest('.node').id.replace('node-', '');
            const portName = port.dataset.port;
            const hasConnection = this.graph.getInputConnection(nodeId, portName);
            port.classList.toggle('connected', !!hasConnection);
        });
    }

    createBezierPath(x1, y1, x2, y2) {
        const dx = Math.abs(x2 - x1);
        const controlOffset = Math.max(50, dx * 0.5);
        return `M ${x1} ${y1} C ${x1 + controlOffset} ${y1}, ${x2 - controlOffset} ${y2}, ${x2} ${y2}`;
    }

    // ============================================
    // Properties Panel
    // ============================================
    showNodeProperties(node) {
        const def = node.definition;
        let html = `<h4 style="margin-bottom: 12px; font-size: 13px;">${def.name}</h4>`;

        for (const input of def.inputs || []) {
            const value = node.values[input.name] ?? input.default ?? 0;
            const hasConnection = this.graph.getInputConnection(node.id, input.name);

            if (hasConnection) {
                html += `
                    <div class="property-row">
                        <label class="property-label">${input.label || input.name}</label>
                        <span style="color: var(--text-muted); font-size: 11px;">Connected</span>
                    </div>
                `;
            } else if (input.type === 'float') {
                html += `
                    <div class="property-row">
                        <label class="property-label">${input.label || input.name}</label>
                        <input type="number" class="property-input" 
                               value="${value}"
                               data-node="${node.id}"
                               data-port="${input.name}"
                               step="0.1"
                               ${input.min !== undefined ? `min="${input.min}"` : ''}
                               ${input.max !== undefined ? `max="${input.max}"` : ''}>
                    </div>
                    ${input.min !== undefined && input.max !== undefined ? `
                        <input type="range" class="property-slider"
                               value="${value}"
                               data-node="${node.id}"
                               data-port="${input.name}"
                               min="${input.min}" max="${input.max}" step="0.01">
                    ` : ''}
                `;
            } else if (input.type === 'int') {
                html += `
                    <div class="property-row">
                        <label class="property-label">${input.label || input.name}</label>
                        <input type="number" class="property-input" 
                               value="${Math.floor(value)}"
                               data-node="${node.id}"
                               data-port="${input.name}"
                               step="1"
                               ${input.min !== undefined ? `min="${input.min}"` : ''}
                               ${input.max !== undefined ? `max="${input.max}"` : ''}>
                    </div>
                `;
            }
        }

        this.elements.propertiesContent.innerHTML = html;
        if (node.type.startsWith('input_pass_')) {
            const label = document.createElement('label');
            label.className = 'property-label';
            label.htmlFor = 'nodePassReference';
            label.textContent = 'Buffer';
            const select = document.createElement('select');
            select.id = 'nodePassReference';
            select.add(new Option('Choose a pass', ''));
            for (const pass of this.passes) select.add(new Option(pass.id, pass.id));
            select.value = node.values.passId || '';
            select.addEventListener('change', () => this.graph.setNodeValue(node.id, 'passId', select.value));
            const note = document.createElement('p');
            note.className = 'preview-note';
            note.textContent = node.type === 'input_pass_current'
                ? 'Latest: earlier stages use current-frame data; this and later stages use history.'
                : node.type === 'input_pass_previous' ? 'Always the previous successful frame.' : 'Actual buffer width, height and aspect.';
            this.elements.propertiesContent.append(label, select, note);
        }

        // Bind events
        this.elements.propertiesContent.querySelectorAll('.property-input, .property-slider').forEach(el => {
            el.addEventListener('input', (e) => {
                const nodeId = e.target.dataset.node;
                const port = e.target.dataset.port;
                const value = parseFloat(e.target.value);
                this.graph.setNodeValue(nodeId, port, value);
                
                // Sync slider/input
                const siblings = this.elements.propertiesContent.querySelectorAll(`[data-node="${nodeId}"][data-port="${port}"]`);
                siblings.forEach(s => { if (s !== e.target) s.value = value; });
            });
        });
    }

    // ============================================
    // Context Menu
    // ============================================
    showContextMenu(e, node) {
        const menu = this.elements.contextMenu;
        menu.style.left = `${e.clientX}px`;
        menu.style.top = `${e.clientY}px`;
        menu.classList.add('visible');

        menu.onclick = (evt) => {
            const action = evt.target.dataset?.action;
            if (action === 'delete') {
                this.graph.removeNode(node.id);
            } else if (action === 'duplicate') {
                this.duplicateNode(node);
            } else if (action === 'disconnect') {
                this.disconnectNode(node);
            }
            this.hideContextMenu();
        };
    }

    hideContextMenu() {
        this.elements.contextMenu.classList.remove('visible');
    }

    duplicateNode(node) {
        const newNode = this.graph.addNode(node.type, {
            x: node.position.x + 30,
            y: node.position.y + 30
        }, this.catalog);
        
        if (newNode) {
            // Copy values
            Object.assign(newNode.values, node.values);
            this.graph.selectNode(newNode.id);
            this.updateCode();
        }
    }

    disconnectNode(node) {
        const edgesToRemove = this.graph.edges.filter(
            e => e.from.nodeId === node.id || e.to.nodeId === node.id
        );
        for (const edge of edgesToRemove) {
            this.graph.disconnect(edge.id);
        }
        this.updateCode();
    }

    // ============================================
    // Drag & Drop from Palette
    // ============================================
    setupDragDrop() {
        this.elements.canvasContainer.addEventListener('dragover', (e) => {
            e.preventDefault();
            e.dataTransfer.dropEffect = 'copy';
        });

        this.elements.canvasContainer.addEventListener('drop', (e) => {
            e.preventDefault();
            const nodeType = e.dataTransfer.getData('nodeType');
            if (!nodeType) return;

            const rect = this.elements.canvasContainer.getBoundingClientRect();
            const x = (e.clientX - rect.left - this.canvas.offsetX) / this.canvas.scale;
            const y = (e.clientY - rect.top - this.canvas.offsetY) / this.canvas.scale;

            this.graph.addNode(nodeType, { x, y }, this.catalog);
            this.updateCode();
        });
    }

    cloneGraph(data) { return JSON.parse(JSON.stringify(data)); }

    stashCanvasGraph() {
        const snapshot = this.cloneGraph(this.graph.serialize());
        if (this.activePass) this.passes.find(p => p.id === this.activePass).graph = snapshot;
        else this.finalPassGraph = snapshot;
    }

    replaceCanvasGraph(data) {
        this.switchingPass = true;
        try { this.graph.deserialize(this.cloneGraph(data), this.catalog); }
        finally { this.switchingPass = false; }
        this.updateSelection();
    }

    refreshPassControls() {
        const select = document.getElementById('passSelection');
        select.replaceChildren(new Option('Draw (final)', ''));
        for (const [index, pass] of this.passes.entries())
            select.add(new Option((index + 1) + ' · ' + pass.id, pass.id), select.options[index] || null);
        select.value = this.activePass;
        const index = this.passes.findIndex(p => p.id === this.activePass);
        const pass = this.passes[index];
        document.getElementById('passSettings').hidden = !pass;
        document.getElementById('btnAddPass').disabled = this.passes.length >= 8;
        document.getElementById('btnPassUp').disabled = index <= 0;
        document.getElementById('btnPassDown').disabled = index < 0 || index === this.passes.length - 1;
        if (pass) {
            document.getElementById('passDivisor').value = String(pass.divisor);
            document.getElementById('passFormat').value = String(pass.format);
        }
        document.getElementById('passStatus').textContent =
            this.passes.length + '/8 persistent passes · Draw stays last';
    }

    selectPass(id) {
        if (id === this.activePass || (id && !this.passes.some(p => p.id === id))) return;
        this.stashCanvasGraph();
        this.activePass = id;
        this.replaceCanvasGraph(id ? this.passes.find(p => p.id === id).graph : this.finalPassGraph);
        this.refreshPassControls();
        this.updateCode();
        this.fitView();
    }

    addPass() {
        if (this.passes.length >= 8) return;
        const input = document.getElementById('passName');
        let id = input.value.trim();
        if (!id) {
            let index = 1;
            while (this.passes.some(p => p.id === 'buffer' + index)) index++;
            id = 'buffer' + index;
        }
        if (!ShaderCodeGenerator.validPassId(id) || this.passes.some(p => p.id === id)) {
            this.showToast('Use a unique lowercase pass identifier (letters, digits, underscore; max 24).', 'error');
            return;
        }
        const graph = new ShaderGraph();
        const source = graph.addNode('input_texture', {x:80,y:120}, this.catalog);
        const out = graph.addNode('output_color', {x:330,y:120}, this.catalog);
        graph.connect(source.id, 'output', out.id, 'color');
        this.passes.push({id, divisor:1, format:1, graph: graph.serialize()});
        input.value = '';
        this.renderNodePalette();
        this.selectPass(id);
    }

    removePass() {
        if (!this.activePass) return;
        this.stashCanvasGraph();
        const id = this.activePass;
        const otherGraphs = [this.finalPassGraph, ...this.passes.filter(p => p.id !== id).map(p => p.graph)];
        if (otherGraphs.some(g => g.nodes.some(n => n.type.startsWith('input_pass_') && n.values?.passId === id))) {
            this.showToast('Remove references to ' + id + ' in other stages first.', 'error');
            return;
        }
        // A user explicitly chooses Remove; the other stage graphs are unchanged.
        if (!confirm('Remove pass ' + id + ' and its graph?')) return;
        this.passes = this.passes.filter(p => p.id !== id);
        this.activePass = '';
        this.replaceCanvasGraph(this.finalPassGraph);
        this.renderNodePalette();
        this.refreshPassControls();
        this.updateCode();
        this.fitView();
    }

    movePass(delta) {
        const index = this.passes.findIndex(p => p.id === this.activePass);
        const next = index + delta;
        if (index < 0 || next < 0 || next >= this.passes.length) return;
        [this.passes[index], this.passes[next]] = [this.passes[next], this.passes[index]];
        this.refreshPassControls();
        this.updateCode();
    }

    setPassDescriptor(key, value) {
        const pass = this.passes.find(p => p.id === this.activePass);
        if (!pass || !(key === 'divisor' ? [1,2,4,8] : key === 'format' ? [0,1] : []).includes(value)) return;
        pass[key] = value;
        this.updateCode();
    }

    serializeProject() {
        if (!this.passes.length) return this.graph.serialize();
        this.stashCanvasGraph();
        return this.cloneGraph({version:2, activePass:this.activePass, passes:this.passes, final:this.finalPassGraph});
    }

    loadProject(data) {
        // Validate a candidate before replacing the active draft; incomplete
        // graphs are allowed to be saved but cannot be exported.
        const validGraph = g => {
            if (!g || g.version !== 1 || !Array.isArray(g.nodes) || !Array.isArray(g.edges))
                throw new Error('Invalid stage graph');
            const ids = new Set();
            for (const n of g.nodes) {
                if (!n || !/^node_[0-9]+$/.test(n.id) || ids.has(n.id) || !this.catalog.findNode(n.type) ||
                    !n.position || !Number.isFinite(n.position.x) || !Number.isFinite(n.position.y))
                    throw new Error('Invalid stage node');
                ids.add(n.id);
            }
            for (const e of g.edges) if (!e || !ids.has(e.from?.nodeId) || !ids.has(e.to?.nodeId))
                throw new Error('Invalid stage edge');
            return this.cloneGraph(g);
        };
        let passes = [], final, active = '';
        if (data.version === 2) {
            if (!Array.isArray(data.passes) || data.passes.length < 1 || data.passes.length > 8)
                throw new Error('Invalid persistent stage count');
            const ids = new Set();
            passes = data.passes.map(p => {
                if (!p || !ShaderCodeGenerator.validPassId(p.id) || ids.has(p.id) ||
                    ![1,2,4,8].includes(p.divisor) || ![0,1].includes(p.format))
                    throw new Error('Invalid or duplicate pass descriptor');
                ids.add(p.id);
                return {id:p.id, divisor:p.divisor, format:p.format, graph:validGraph(p.graph)};
            });
            final = validGraph(data.final);
            active = ids.has(data.activePass) ? data.activePass : '';
        } else {
            final = validGraph(data);
        }
        this.passes = passes; this.finalPassGraph = final; this.activePass = active;
        this.replaceCanvasGraph(active ? passes.find(p => p.id === active).graph : final);
        this.renderNodePalette();
        this.refreshPassControls();
        this.updateCode();
        this.fitView();
    }

    generateProjectCode() {
        if (!this.passes.length) return this.codegen.generate(this.graph);
        const materialize = snapshot => {
            const g = new ShaderGraph();
            g.deserialize(this.cloneGraph(snapshot), this.catalog);
            return g;
        };
        return this.codegen.generatePersistent(this.passes.map(p => ({
            ...p, graph:p.id === this.activePass ? this.graph : materialize(p.graph)
        })), this.activePass ? materialize(this.finalPassGraph) : this.graph);
    }

    // ============================================
    // Code Generation
    // ============================================
    updateCode() {
        if (this.switchingPass) return;
        let code, valid = true;
        try { code = this.generateProjectCode(); }
        catch (error) { code = '// ' + error.message; valid = false; }
        this.elements.codePreview.querySelector('code').textContent = code;
        document.getElementById('btnExport').disabled = !valid;
        document.getElementById('btnCopyCode').disabled = !valid;
        this.updatePreview();
        this.scheduleRecoverySave();
    }

    showPreviewMessage(message) {
        const overlay = this.elements.previewOverlay;
        overlay.querySelector('span').textContent = message;
        overlay.classList.remove('hidden');
    }

    updatePreview() {
        if (!this.previewRenderer) {
            this.showPreviewMessage(this.previewError || 'WebGL2 preview unavailable');
            return;
        }
        if (this.passes.length) {
            this.previewRenderer.clear();
            this.showPreviewMessage('Persistent simulation preview requires native OBS. Export the complete graph to render it.');
            return;
        }
        if (!this.graph.getOutputNode()) {
            this.previewRenderer.clear();
            this.showPreviewMessage('Add a Final Output node to preview');
            return;
        }
        try {
            const source = this.codegen.buildPreviewShader(this.graph);
            this.previewRenderer.setShader(source);
            this.elements.previewOverlay.classList.add('hidden');
        } catch (error) {
            this.previewRenderer.clear();
            this.showPreviewMessage(error.message || 'Preview compilation failed');
        }
    }

    copyCode() {
        let code; try { code = this.generateProjectCode(); } catch (error) { this.showToast(error.message, "error"); return; }
        navigator.clipboard.writeText(code).then(() => {
            this.showToast('Code copied to clipboard!', 'success');
        });
    }

    // ============================================
    // File Operations
    // ============================================
    newGraph() {
        if (this.graph.nodes.size > 0 || this.passes.length) {
            if (!confirm('Create new graph? Unsaved changes will be lost.')) return;
        }
        this.clearRecoveryDraft();
        this.passes = []; this.activePass = ''; this.finalPassGraph = {version:1,nodes:[],edges:[]};
        this.replaceCanvasGraph(this.finalPassGraph);
        this.renderNodePalette();
        this.refreshPassControls();
        this.elements.shaderName.value = 'Untitled Shader';
        this.updateCode();
        this.fitView();
        this.showToast('New graph created', 'success');
    }

    saveGraph() {
        const data = {
            name: this.elements.shaderName.value,
            ...this.serializeProject()
        };
        const json = JSON.stringify(data, null, 2);
        const blob = new Blob([json], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = `${this.elements.shaderName.value.replace(/\s+/g, '_')}.hsgraph`;
        a.click();
        
        URL.revokeObjectURL(url);
        this.showToast('Graph saved!', 'success');
    }

    loadGraph() {
        document.getElementById('fileInput').click();
    }

    handleFileLoad(e) {
        const file = e.target.files[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = (evt) => {
            try {
                const data = JSON.parse(evt.target.result);
                this.loadProject(data);
                this.clearRecoveryDraft();
                this.elements.shaderName.value = data.name || 'Loaded Shader';
                this.updateCode();
                this.fitView();
                this.showToast('Graph loaded!', 'success');
            } catch (err) {
                this.showToast('Failed to load graph: ' + err.message, 'error');
            }
        };
        reader.readAsText(file);
        e.target.value = '';
    }

    exportShader() {
        let code; try { code = this.generateProjectCode(); } catch (error) { this.showToast(error.message, "error"); return; }
        const blob = new Blob([code], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        
        const a = document.createElement('a');
        a.href = url;
        a.download = `${this.elements.shaderName.value.replace(/\s+/g, '_')}.effect`;
        a.click();
        
        URL.revokeObjectURL(url);
        this.showToast('Shader exported!', 'success');
    }

    // ============================================
    // Keyboard Shortcuts
    // ============================================
    onKeyDown(e) {
        // Delete selected
        if (e.key === 'Delete' || e.key === 'Backspace') {
            if (['INPUT','TEXTAREA','SELECT'].includes(document.activeElement.tagName)) return;
            for (const nodeId of this.graph.selectedNodes) {
                this.graph.removeNode(nodeId);
            }
        }

        // Ctrl+S - Save
        if (e.ctrlKey && e.key === 's') {
            e.preventDefault();
            this.saveGraph();
        }

        // Ctrl+E - Export
        if (e.ctrlKey && e.key === 'e') {
            e.preventDefault();
            this.exportShader();
        }

        // Escape - Deselect
        if (e.key === 'Escape') {
            this.graph.deselectAll();
            this.hideContextMenu();
            if (this.connecting.active) {
                if (this.connecting.tempLine) this.connecting.tempLine.remove();
                this.connecting = { active: false, fromNode: null, fromPort: null, isOutput: false, tempLine: null };
            }
        }
    }

    // ============================================
    // Toast Notifications
    // ============================================
    showToast(message, type = 'info') {
        const toast = document.createElement('div');
        toast.className = `toast ${type}`;
        toast.textContent = message;
        this.elements.toastContainer.appendChild(toast);

        setTimeout(() => {
            toast.style.opacity = '0';
            setTimeout(() => toast.remove(), 300);
        }, 3000);
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new ShaderBuilderApp();
    window.app.setupDragDrop();
    
    // Add some default nodes for demo
    setTimeout(() => {
        const uvNode = window.app.graph.addNode('input_uv', { x: 100, y: 150 }, window.app.catalog);
        const timeNode = window.app.graph.addNode('input_time', { x: 100, y: 50 }, window.app.catalog);
        const swirlNode = window.app.graph.addNode('distort_swirl', { x: 350, y: 100 }, window.app.catalog);
        const sampleNode = window.app.graph.addNode('tex_sample', { x: 600, y: 100 }, window.app.catalog);
        const outputNode = window.app.graph.addNode('output_color', { x: 850, y: 100 }, window.app.catalog);

        // Connect demo nodes
        if (uvNode && swirlNode) {
            window.app.graph.connect(uvNode.id, 'output', swirlNode.id, 'uv');
        }
        if (swirlNode && sampleNode) {
            window.app.graph.connect(swirlNode.id, 'result', sampleNode.id, 'uv');
        }
        if (sampleNode && outputNode) {
            window.app.graph.connect(sampleNode.id, 'result', outputNode.id, 'color');
        }

        window.app.updateCode();
        window.app.fitView();
    }, 100);
    
    // ============================================
    // Qt WebChannel Integration (OBS Dock Mode)
    // ============================================
    initQtWebChannel();
});

/**
 * Initialize Qt WebChannel for communication with OBS
 * This enables native export when running inside the OBS dock
 */
function initQtWebChannel() {
    // Check if running inside Qt WebEngine
    if (typeof qt === 'undefined' || !qt.webChannelTransport) {
        console.log('[ShaderBuilder] Running in standalone mode (no Qt WebChannel)');
        return;
    }
    
    console.log('[ShaderBuilder] Qt WebChannel detected, initializing bridge...');
    
    // Load QWebChannel library
    const script = document.createElement('script');
    script.src = 'qrc:///qtwebchannel/qwebchannel.js';
    script.onload = () => {
        new QWebChannel(qt.webChannelTransport, function(channel) {
            window.shaderBridge = channel.objects.bridge;
            
            if (window.shaderBridge) {
                console.log('[ShaderBuilder] Bridge connected to OBS!');
                console.log('[ShaderBuilder] Data path:', window.shaderBridge.dataPath);
                
                // Update UI to show OBS mode
                document.body.classList.add('obs-mode');
                
                // Add "Export to OBS" indicator
                const exportBtn = document.getElementById('btnExport');
                if (exportBtn) {
                    exportBtn.textContent = '📤 Export to OBS';
                    exportBtn.title = 'Export shader directly to hypershader plugin';
                }
                
                // Listen for shader saved events
                window.shaderBridge.shaderSaved.connect(function(path, success) {
                    if (success) {
                        window.app.showToast(`Shader saved to OBS: ${path}`, 'success');
                        window.app.clearRecoveryDraft();
                    } else {
                        window.app.showToast(`Failed to save shader: ${path}`, 'error');
                    }
                });

                const recoveredIds = window.shaderBridge.getRecoveredDraftIds();
                if (recoveredIds && recoveredIds.length > 0) {
                    const draftId = recoveredIds[0];
                    const payload = window.shaderBridge.loadRecoveredDraft(draftId);
                    if (payload) {
                        try {
                            const data = JSON.parse(payload);
                            window.app.showToast('Recovered shader draft available', 'info');
                            if (confirm('A recovered shader draft was found. Restore it?')) {
                                window.app.loadProject(data);
                                window.app.elements.shaderName.value = data.name || 'Recovered Shader';
                                window.app.recoveryDraftId = draftId;
                                window.app.updateCode();
                                window.app.fitView();
                                window.app.showToast('Recovered unsaved shader draft', 'success');
                            }
                        } catch (err) {
                            console.warn('[ShaderBuilder] Failed to restore recovered draft:', err);
                        }
                    }
                }

                // Theme synchronization
                const updateTheme = (css) => {
                    let styleEl = document.getElementById('obs-theme-styles');
                    if (!styleEl) {
                        styleEl = document.createElement('style');
                        styleEl.id = 'obs-theme-styles';
                        document.head.appendChild(styleEl);
                    }
                    styleEl.textContent = css;
                    console.log('[ShaderBuilder] Theme applied from OBS');
                };

                window.shaderBridge.themeChanged.connect(updateTheme);
            }
        });
    };
    script.onerror = () => {
        console.warn('[ShaderBuilder] Failed to load QWebChannel, using fallback');
    };
    document.head.appendChild(script);
}

/**
 * Override export function when in OBS mode
 * Called from ShaderBuilderApp.exportShader()
 */
ShaderBuilderApp.prototype.exportShaderNative = function() {
    if (window.shaderBridge) {
        let code; try { code = this.generateProjectCode(); } catch (error) { this.showToast(error.message, "error"); return; }
        const name = this.elements.shaderName.value || 'custom_shader';
        
        // Use native bridge to save directly to OBS data directory
        window.shaderBridge.exportShader(code, name);
        window.shaderBridge.log('Shader exported: ' + name, 0);
        this.clearRecoveryDraft();
    } else {
        // Fallback to download
        this.exportShader();
    }
};

// Patch the export button to use native export when available
const originalExportShader = ShaderBuilderApp.prototype.exportShader;
ShaderBuilderApp.prototype.exportShader = function() {
    if (window.shaderBridge) {
        this.exportShaderNative();
    } else {
        originalExportShader.call(this);
    }
};
