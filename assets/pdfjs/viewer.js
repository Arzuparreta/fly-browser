/* viewer.js: Tailored Mozilla PDF.js presentation engine for Fly Browser */

(function() {
    'use strict';

    let pdfDoc = null;
    let totalPages = 0;
    let currentPage = 1;
    let currentScale = 1.0;
    let isFitWidth = true;
    let rawPdfBytes = null;
    let isRendering = new Map();
    let renderedPages = new Set();
    let pageViewports = new Map();
    let intersectionObserver = null;

    const urlParams = new URLSearchParams(window.location.search);
    const fileUrl = urlParams.get('file');
    const token = urlParams.get('token');

    const elements = {
        title: document.getElementById('doc-title'),
        prevBtn: document.getElementById('btn-prev'),
        nextBtn: document.getElementById('btn-next'),
        pageInput: document.getElementById('input-page'),
        pageTotal: document.getElementById('page-total'),
        zoomOutBtn: document.getElementById('btn-zoom-out'),
        zoomInBtn: document.getElementById('btn-zoom-in'),
        zoomFitBtn: document.getElementById('btn-zoom-fit'),
        darkToggleBtn: document.getElementById('btn-dark-toggle'),
        saveBtn: document.getElementById('btn-save'),
        pagesContainer: document.getElementById('pages-container'),
        loadingOverlay: document.getElementById('loading-overlay'),
        loadingMessage: document.getElementById('loading-message'),
        errorOverlay: document.getElementById('error-overlay'),
        errorMessage: document.getElementById('error-message')
    };

    function extractDocumentName(url) {
        if (!url) return 'Document.pdf';
        try {
            const cleanUrl = url.split('?')[0].split('#')[0];
            const parts = cleanUrl.split('/');
            const last = parts[parts.length - 1];
            return decodeURIComponent(last) || 'Document.pdf';
        } catch (e) {
            return 'Document.pdf';
        }
    }

    const documentName = extractDocumentName(fileUrl);
    document.title = `${documentName} · Fly PDF`;
    if (elements.title) {
        elements.title.textContent = documentName;
        elements.title.title = fileUrl || documentName;
    }

    function showError(message) {
        if (elements.loadingOverlay) elements.loadingOverlay.style.display = 'none';
        if (elements.errorOverlay) {
            elements.errorOverlay.style.display = 'flex';
            if (elements.errorMessage) elements.errorMessage.textContent = message;
        }
    }

    async function loadPdfData() {
        if (!fileUrl) {
            showError('No PDF URL specified in request.');
            return;
        }

        try {
            if (window.__misePDFBridge && typeof window.__misePDFBridge.loadPdfData === 'function') {
                rawPdfBytes = await window.__misePDFBridge.loadPdfData(token, fileUrl);
            } else {
                const response = await fetch(fileUrl);
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status} ${response.statusText}`);
                }
                const buffer = await response.arrayBuffer();
                rawPdfBytes = new Uint8Array(buffer);
            }

            if (!rawPdfBytes || rawPdfBytes.length === 0) {
                throw new Error('Received empty PDF payload.');
            }

            initialiseViewer();
        } catch (err) {
            console.error('Failed to load PDF data:', err);
            showError(`Failed to load PDF: ${err.message || 'Network or file error'}`);
        }
    }

    async function initialiseViewer() {
        try {
            pdfjsLib.GlobalWorkerOptions.workerSrc = './pdf.worker.min.js';

            const loadingTask = pdfjsLib.getDocument({
                data: rawPdfBytes instanceof Uint8Array ? rawPdfBytes : new Uint8Array(rawPdfBytes),
                enableScripting: false,
                isEvalSupported: false
            });

            pdfDoc = await loadingTask.promise;
            totalPages = pdfDoc.numPages;

            if (elements.pageTotal) elements.pageTotal.textContent = totalPages;
            if (elements.pageInput) elements.pageInput.max = totalPages;

            // Fetch base dimension from page 1 to calculate initial fit width
            const firstPage = await pdfDoc.getPage(1);
            const initialViewport = firstPage.getViewport({ scale: 1.0 });
            pageViewports.set(1, initialViewport);

            computeFitWidthScale(initialViewport);
            buildPagePlaceholders(initialViewport);
            setupIntersectionObserver();

            // Render first page immediately for instant display
            const firstPageEl = document.getElementById('pdf-page-1');
            if (firstPageEl) {
                renderPage(1, firstPageEl);
            }

            if (elements.loadingOverlay) {
                elements.loadingOverlay.style.display = 'none';
            }
        } catch (err) {
            console.error('Failed to parse PDF document:', err);
            showError(`Failed to parse PDF: ${err.message || 'Corrupt format'}`);
        }
    }

    function computeFitWidthScale(baseViewport) {
        if (!isFitWidth || !baseViewport) return;
        const availableWidth = Math.max(320, window.innerWidth - 64);
        const targetScale = availableWidth / baseViewport.width;
        currentScale = Math.min(3.0, Math.max(0.5, targetScale));
        if (elements.zoomFitBtn) {
            elements.zoomFitBtn.textContent = `${Math.round(currentScale * 100)}%`;
        }
    }

    function buildPagePlaceholders(firstViewport) {
        elements.pagesContainer.innerHTML = '';
        renderedPages.clear();
        isRendering.clear();

        for (let i = 1; i <= totalPages; i++) {
            const wrapper = document.createElement('div');
            wrapper.className = 'pdf-page-wrapper';
            wrapper.id = `pdf-page-${i}`;
            wrapper.dataset.pageNumber = i;

            const vp = pageViewports.get(i) || firstViewport;
            const scaledWidth = Math.floor(vp.width * currentScale);
            const scaledHeight = Math.floor(vp.height * currentScale);

            wrapper.style.width = `${scaledWidth}px`;
            wrapper.style.height = `${scaledHeight}px`;

            const placeholder = document.createElement('div');
            placeholder.className = 'page-placeholder';
            placeholder.textContent = `Page ${i}`;
            wrapper.appendChild(placeholder);

            const canvas = document.createElement('canvas');
            canvas.className = 'pdf-page-canvas';
            canvas.style.display = 'none';
            wrapper.appendChild(canvas);

            const textLayer = document.createElement('div');
            textLayer.className = 'textLayer';
            wrapper.appendChild(textLayer);

            elements.pagesContainer.appendChild(wrapper);
        }
    }

    function setupIntersectionObserver() {
        if (intersectionObserver) {
            intersectionObserver.disconnect();
        }

        intersectionObserver = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                const pageNum = parseInt(entry.target.dataset.pageNumber, 10);
                if (entry.isIntersecting) {
                    renderPage(pageNum, entry.target);
                }
            });
        }, {
            root: null,
            rootMargin: '600px 0px 600px 0px',
            threshold: 0.01
        });

        const pages = elements.pagesContainer.querySelectorAll('.pdf-page-wrapper');
        pages.forEach(p => intersectionObserver.observe(p));
    }

    async function renderPage(pageNum, wrapperEl) {
        if (renderedPages.has(pageNum) || isRendering.get(pageNum)) {
            return;
        }

        isRendering.set(pageNum, true);

        try {
            const page = await pdfDoc.getPage(pageNum);
            const viewport = page.getViewport({ scale: currentScale });
            pageViewports.set(pageNum, page.getViewport({ scale: 1.0 }));

            const scaledWidth = Math.floor(viewport.width);
            const scaledHeight = Math.floor(viewport.height);
            wrapperEl.style.width = `${scaledWidth}px`;
            wrapperEl.style.height = `${scaledHeight}px`;

            const canvas = wrapperEl.querySelector('.pdf-page-canvas');
            const placeholder = wrapperEl.querySelector('.page-placeholder');
            const textLayer = wrapperEl.querySelector('.textLayer');

            const outputScale = window.devicePixelRatio || 1;
            canvas.width = Math.floor(viewport.width * outputScale);
            canvas.height = Math.floor(viewport.height * outputScale);
            canvas.style.width = `${scaledWidth}px`;
            canvas.style.height = `${scaledHeight}px`;

            const ctx = canvas.getContext('2d', { alpha: false });
            ctx.imageSmoothingEnabled = true;

            const renderContext = {
                canvasContext: ctx,
                transform: outputScale !== 1 ? [outputScale, 0, 0, outputScale, 0, 0] : null,
                viewport: viewport
            };

            await page.render(renderContext).promise;

            canvas.style.display = 'block';
            if (placeholder) placeholder.style.display = 'none';

            // Extract and position text layer for selection and Fly find in page
            try {
                const textContent = await page.getTextContent();
                textLayer.innerHTML = '';
                textLayer.style.width = `${scaledWidth}px`;
                textLayer.style.height = `${scaledHeight}px`;
                textLayer.style.setProperty('--scale-factor', viewport.scale);

                if (typeof pdfjsLib.renderTextLayer === 'function') {
                    const textLayerTask = pdfjsLib.renderTextLayer({
                        textContentSource: textContent,
                        container: textLayer,
                        viewport: viewport,
                        textDivs: []
                    });
                    if (textLayerTask && typeof textLayerTask.promise !== 'undefined') {
                        await textLayerTask.promise;
                    }
                }
            } catch (textErr) {
                console.warn(`Text layer render skipped on page ${pageNum}:`, textErr);
            }

            renderedPages.add(pageNum);
        } catch (err) {
            console.error(`Error rendering page ${pageNum}:`, err);
        } finally {
            isRendering.set(pageNum, false);
        }
    }

    function updateActivePageIndicator() {
        const pages = elements.pagesContainer.querySelectorAll('.pdf-page-wrapper');
        const viewCenter = window.scrollY + window.innerHeight / 2;

        let closestPage = 1;
        let smallestDistance = Infinity;

        pages.forEach(wrapper => {
            const pageNum = parseInt(wrapper.dataset.pageNumber, 10);
            const box = wrapper.getBoundingClientRect();
            const pageCenter = window.scrollY + box.top + box.height / 2;
            const dist = Math.abs(viewCenter - pageCenter);
            if (dist < smallestDistance) {
                smallestDistance = dist;
                closestPage = pageNum;
            }
        });

        if (closestPage !== currentPage) {
            currentPage = closestPage;
            if (elements.pageInput && document.activeElement !== elements.pageInput) {
                elements.pageInput.value = currentPage;
            }
        }
    }

    window.addEventListener('scroll', updateActivePageIndicator, { passive: true });

    function scrollToPage(pageNum) {
        const targetNum = Math.min(totalPages, Math.max(1, pageNum));
        const targetEl = document.getElementById(`pdf-page-${targetNum}`);
        if (targetEl) {
            targetEl.scrollIntoView({ behavior: 'smooth', block: 'start' });
            currentPage = targetNum;
            if (elements.pageInput) elements.pageInput.value = targetNum;
        }
    }

    function applyZoom(newScale, fit = false) {
        isFitWidth = fit;
        currentScale = Math.min(3.0, Math.max(0.5, newScale));
        if (elements.zoomFitBtn) {
            elements.zoomFitBtn.textContent = fit ? 'Fit' : `${Math.round(currentScale * 100)}%`;
        }

        const firstViewport = pageViewports.get(1);
        if (firstViewport) {
            buildPagePlaceholders(firstViewport);
            setupIntersectionObserver();
            scrollToPage(currentPage);
        }
    }

    // UI event listeners
    if (elements.prevBtn) {
        elements.prevBtn.addEventListener('click', () => scrollToPage(currentPage - 1));
    }

    if (elements.nextBtn) {
        elements.nextBtn.addEventListener('click', () => scrollToPage(currentPage + 1));
    }

    if (elements.pageInput) {
        elements.pageInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                const val = parseInt(elements.pageInput.value, 10);
                if (!isNaN(val)) scrollToPage(val);
                elements.pageInput.blur();
            } else if (e.key === 'Escape') {
                elements.pageInput.blur();
            }
        });
    }

    if (elements.zoomInBtn) {
        elements.zoomInBtn.addEventListener('click', () => applyZoom(currentScale + 0.25, false));
    }

    if (elements.zoomOutBtn) {
        elements.zoomOutBtn.addEventListener('click', () => applyZoom(currentScale - 0.25, false));
    }

    if (elements.zoomFitBtn) {
        elements.zoomFitBtn.addEventListener('click', () => {
            const vp = pageViewports.get(1);
            if (vp) {
                computeFitWidthScale(vp);
                applyZoom(currentScale, true);
            }
        });
    }

    if (elements.darkToggleBtn) {
        elements.darkToggleBtn.addEventListener('click', () => {
            document.body.classList.toggle('dark-doc');
        });
    }

    if (elements.saveBtn) {
        elements.saveBtn.addEventListener('click', async () => {
            if (window.__misePDFBridge && typeof window.__misePDFBridge.savePdfFile === 'function') {
                elements.saveBtn.disabled = true;
                elements.saveBtn.textContent = 'Saving...';
                try {
                    await window.__misePDFBridge.savePdfFile(token, {
                        url: fileUrl,
                        suggestedName: documentName,
                        data: rawPdfBytes
                    });
                } finally {
                    elements.saveBtn.disabled = false;
                    elements.saveBtn.textContent = '⤓ Save';
                }
            }
        });
    }

    // Window resize handler for responsive fit width
    let resizeTimer = null;
    window.addEventListener('resize', () => {
        if (!isFitWidth) return;
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(() => {
            const vp = pageViewports.get(1);
            if (vp) {
                computeFitWidthScale(vp);
                applyZoom(currentScale, true);
            }
        }, 150);
    });

    // Supplementary non-colliding shortcuts
    window.addEventListener('keydown', (e) => {
        if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) {
            return;
        }

        if (e.ctrlKey && e.shiftKey && e.key === 'S') {
            e.preventDefault();
            if (elements.saveBtn) elements.saveBtn.click();
            return;
        }

        if (e.ctrlKey || e.altKey || e.metaKey) {
            return;
        }

        switch (e.key) {
            case '[':
                e.preventDefault();
                scrollToPage(currentPage - 1);
                break;
            case ']':
                e.preventDefault();
                scrollToPage(currentPage + 1);
                break;
            case '+':
            case '=':
                e.preventDefault();
                applyZoom(currentScale + 0.25, false);
                break;
            case '-':
                e.preventDefault();
                applyZoom(currentScale - 0.25, false);
                break;
            case '0':
                e.preventDefault();
                const vp = pageViewports.get(1);
                if (vp) {
                    computeFitWidthScale(vp);
                    applyZoom(currentScale, true);
                }
                break;
        }
    });

    loadPdfData();
})();
