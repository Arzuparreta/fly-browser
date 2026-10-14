import { escapeHtml } from './utils.js';

function getFileIcon(filename) {
    if (!filename) return 'fa-regular fa-file';
    const ext = filename.split('.').pop().toLowerCase();
    const iconMap = {
        png: 'fa-regular fa-file-image',
        jpg: 'fa-regular fa-file-image',
        jpeg: 'fa-regular fa-file-image',
        gif: 'fa-regular fa-file-image',
        webp: 'fa-regular fa-file-image',
        svg: 'fa-regular fa-file-image',
        ico: 'fa-regular fa-file-image',
        bmp: 'fa-regular fa-file-image',
        zip: 'fa-regular fa-file-zipper',
        tar: 'fa-regular fa-file-zipper',
        gz: 'fa-regular fa-file-zipper',
        xz: 'fa-regular fa-file-zipper',
        '7z': 'fa-regular fa-file-zipper',
        bz2: 'fa-regular fa-file-zipper',
        rar: 'fa-regular fa-file-zipper',
        pdf: 'fa-regular fa-file-pdf',
        js: 'fa-regular fa-file-code',
        ts: 'fa-regular fa-file-code',
        json: 'fa-regular fa-file-code',
        html: 'fa-regular fa-file-code',
        css: 'fa-regular fa-file-code',
        py: 'fa-regular fa-file-code',
        c: 'fa-regular fa-file-code',
        cpp: 'fa-regular fa-file-code',
        rs: 'fa-regular fa-file-code',
        go: 'fa-regular fa-file-code',
        sh: 'fa-regular fa-file-code',
        zsh: 'fa-regular fa-file-code',
        md: 'fa-regular fa-file-lines',
        txt: 'fa-regular fa-file-lines',
        doc: 'fa-regular fa-file-word',
        docx: 'fa-regular fa-file-word',
        xls: 'fa-regular fa-file-excel',
        xlsx: 'fa-regular fa-file-excel',
        mp3: 'fa-regular fa-file-audio',
        wav: 'fa-regular fa-file-audio',
        flac: 'fa-regular fa-file-audio',
        ogg: 'fa-regular fa-file-audio',
        mp4: 'fa-regular fa-file-video',
        mkv: 'fa-regular fa-file-video',
        webm: 'fa-regular fa-file-video',
        deb: 'fa-solid fa-gears',
        rpm: 'fa-solid fa-gears',
        pkg: 'fa-solid fa-gears',
        appimage: 'fa-solid fa-gears',
        exe: 'fa-solid fa-gears'
    };
    return iconMap[ext] || 'fa-regular fa-file';
}

export function formatBytes(bytes) {
    if (!bytes || bytes <= 0) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(1024));
    const clampedIndex = Math.min(i, units.length - 1);
    const val = (bytes / Math.pow(1024, clampedIndex)).toFixed(clampedIndex === 0 ? 0 : 1);
    return `${val} ${units[clampedIndex]}`;
}

export function formatSpeed(bytesPerSec) {
    if (!bytesPerSec || bytesPerSec <= 0) return '0 B/s';
    const units = ['B/s', 'KB/s', 'MB/s', 'GB/s'];
    const i = Math.floor(Math.log(bytesPerSec) / Math.log(1024));
    const clampedIndex = Math.min(i, units.length - 1);
    const val = (bytesPerSec / Math.pow(1024, clampedIndex)).toFixed(1);
    return `${val} ${units[clampedIndex]}`;
}

export function showDownloadShelf() {
    const shelf = document.getElementById('DownloadShelf');
    if (shelf) shelf.style.display = 'flex';
}

export function hideDownloadShelf() {
    const shelf = document.getElementById('DownloadShelf');
    if (shelf) shelf.style.display = 'none';
}

export function toggleDownloadShelf() {
    const shelf = document.getElementById('DownloadShelf');
    if (!shelf) return;
    if (shelf.style.display === 'none' || !shelf.style.display) {
        shelf.style.display = 'flex';
    } else {
        shelf.style.display = 'none';
    }
}

function checkShelfEmpty() {
    const itemsContainer = document.getElementById('DownloadShelfItems');
    const shelf = document.getElementById('DownloadShelf');
    if (!itemsContainer || !shelf) return;
    if (itemsContainer.children.length === 0) {
        shelf.style.display = 'none';
    }
}

export function clearCompletedDownloads() {
    const itemsContainer = document.getElementById('DownloadShelfItems');
    if (!itemsContainer) return;
    const cards = itemsContainer.querySelectorAll('.download-card.completed, .download-card.cancelled, .download-card.interrupted');
    cards.forEach(card => card.remove());
    checkShelfEmpty();
}

export function getEffectiveFilename(savePath, fallbackFilename) {
    if (savePath && typeof savePath === 'string') {
        const cleaned = savePath.trim().replace(/\/+$/, '');
        const base = cleaned.split('/').pop();
        if (base && base.length > 0) {
            return base;
        }
    }
    return fallbackFilename || 'download';
}

function createDownloadCardElement(data) {
    const card = document.createElement('div');
    card.id = `dl-card-${data.id}`;
    card.className = 'download-card progressing';
    card.setAttribute('data-id', data.id);
    if (data.savePath) {
        card.setAttribute('data-save-path', data.savePath);
    }

    const currentSavePath = data.savePath || '';
    const currentFilename = getEffectiveFilename(currentSavePath, data.filename);
    card.title = currentSavePath ? `${currentFilename}\n${currentSavePath}` : currentFilename;

    const iconClass = getFileIcon(currentFilename);
    const initialStatus = data.totalBytes > 0 
        ? `0% · ${formatBytes(data.totalBytes)}` 
        : 'Starting...';

    card.innerHTML = `
        <div class="download-card-icon">
            <i class="${iconClass}"></i>
        </div>
        <div class="download-card-content">
            <div class="download-card-title">${escapeHtml(currentFilename)}</div>
            <div class="download-card-meta">
                <span class="download-card-status">${initialStatus}</span>
            </div>
            <div class="download-card-progress-track">
                <div class="download-card-progress-bar" style="width: 0%;"></div>
            </div>
        </div>
        <div class="download-card-actions">
            <button class="download-action-btn cancel-btn" title="Cancel Download"><i class="fa-solid fa-xmark"></i></button>
        </div>
    `;

    const cancelBtn = card.querySelector('.cancel-btn');
    if (cancelBtn) {
        cancelBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            if (window.flyAPI && typeof window.flyAPI.cancelDownload === 'function') {
                window.flyAPI.cancelDownload(data.id);
            }
        });
    }

    return card;
}

export function handleDownloadStarted(data) {
    const shelf = document.getElementById('DownloadShelf');
    const itemsContainer = document.getElementById('DownloadShelfItems');
    if (!shelf || !itemsContainer) return;

    shelf.style.display = 'flex';

    let card = document.getElementById(`dl-card-${data.id}`);
    if (!card) {
        card = createDownloadCardElement(data);
        itemsContainer.prepend(card);
    }
}

export function handleDownloadProgress(data) {
    let card = document.getElementById(`dl-card-${data.id}`);
    if (!card) {
        handleDownloadStarted(data);
        card = document.getElementById(`dl-card-${data.id}`);
        if (!card) return;
    }

    if (data.savePath) {
        card.setAttribute('data-save-path', data.savePath);
    }

    const currentSavePath = data.savePath || card.getAttribute('data-save-path') || '';
    const currentFilename = getEffectiveFilename(currentSavePath, data.filename);

    const titleElem = card.querySelector('.download-card-title');
    if (titleElem && titleElem.textContent !== currentFilename) {
        titleElem.textContent = currentFilename;
    }

    const iconElem = card.querySelector('.download-card-icon i');
    if (iconElem) {
        iconElem.className = getFileIcon(currentFilename);
    }

    card.title = currentSavePath ? `${currentFilename}\n${currentSavePath}` : currentFilename;

    const progressBar = card.querySelector('.download-card-progress-bar');
    const statusSpan = card.querySelector('.download-card-status');
    const percent = data.percent || 0;

    if (progressBar) {
        progressBar.style.width = `${percent}%`;
    }

    if (statusSpan) {
        if (data.state === 'interrupted') {
            statusSpan.textContent = 'Interrupted';
            card.classList.remove('progressing');
            card.classList.add('interrupted');
        } else if (data.isPaused) {
            statusSpan.textContent = `${percent}% (Paused)`;
        } else if (data.totalBytes > 0) {
            statusSpan.textContent = `${percent}% · ${formatBytes(data.receivedBytes)} / ${formatBytes(data.totalBytes)} · ${formatSpeed(data.speed)}`;
        } else {
            statusSpan.textContent = `${formatBytes(data.receivedBytes)} · ${formatSpeed(data.speed)}`;
        }
    }
}

export function handleDownloadDone(data) {
    let card = document.getElementById(`dl-card-${data.id}`);
    if (!card) {
        handleDownloadStarted(data);
        card = document.getElementById(`dl-card-${data.id}`);
        if (!card) return;
    }

    if (data.savePath) {
        card.setAttribute('data-save-path', data.savePath);
    }

    const currentSavePath = data.savePath || card.getAttribute('data-save-path') || '';
    const currentFilename = getEffectiveFilename(currentSavePath, data.filename);

    card.classList.remove('progressing');
    const progressBar = card.querySelector('.download-card-progress-bar');
    const statusSpan = card.querySelector('.download-card-status');
    const actionsContainer = card.querySelector('.download-card-actions');
    const progressTrack = card.querySelector('.download-card-progress-track');
    const titleElem = card.querySelector('.download-card-title');
    const iconElem = card.querySelector('.download-card-icon i');

    if (titleElem) titleElem.textContent = currentFilename;
    if (iconElem) iconElem.className = getFileIcon(currentFilename);
    card.title = currentSavePath ? `${currentFilename}\n${currentSavePath}` : currentFilename;

    if (data.state === 'completed') {
        card.classList.add('completed');
        if (progressBar) progressBar.style.width = '100%';
        if (progressTrack) progressTrack.style.display = 'none';

        const sizeStr = formatBytes(data.totalBytes || data.receivedBytes);
        if (statusSpan) statusSpan.textContent = `${sizeStr} · Complete`;

        if (actionsContainer) {
            actionsContainer.innerHTML = `
                <button class="download-action-btn open-btn" title="Open File"><i class="fa-solid fa-arrow-up-right-from-square"></i></button>
                <button class="download-action-btn reveal-btn" title="Show in File Manager"><i class="fa-solid fa-folder-open"></i></button>
                <button class="download-action-btn dismiss-btn" title="Dismiss"><i class="fa-solid fa-xmark"></i></button>
            `;

            const openBtn = actionsContainer.querySelector('.open-btn');
            if (openBtn) {
                openBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    const activePath = card.getAttribute('data-save-path') || currentSavePath;
                    if (activePath && window.flyAPI && typeof window.flyAPI.openDownload === 'function') {
                        window.flyAPI.openDownload(activePath);
                    }
                });
            }

            const revealBtn = actionsContainer.querySelector('.reveal-btn');
            if (revealBtn) {
                revealBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    const activePath = card.getAttribute('data-save-path') || currentSavePath;
                    if (activePath && window.flyAPI && typeof window.flyAPI.revealDownload === 'function') {
                        window.flyAPI.revealDownload(activePath);
                    }
                });
            }

            const dismissBtn = actionsContainer.querySelector('.dismiss-btn');
            if (dismissBtn) {
                dismissBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    card.remove();
                    checkShelfEmpty();
                });
            }
        }

        const content = card.querySelector('.download-card-content');
        if (content) {
            content.style.cursor = 'pointer';
            content.onclick = () => {
                const activePath = card.getAttribute('data-save-path') || currentSavePath;
                if (activePath && window.flyAPI && typeof window.flyAPI.openDownload === 'function') {
                    window.flyAPI.openDownload(activePath);
                }
            };
        }
    } else if (data.state === 'cancelled') {
        card.classList.add('cancelled');
        if (progressTrack) progressTrack.style.display = 'none';
        if (statusSpan) statusSpan.textContent = 'Cancelled';
        if (actionsContainer) {
            actionsContainer.innerHTML = `
                <button class="download-action-btn dismiss-btn" title="Dismiss"><i class="fa-solid fa-xmark"></i></button>
            `;
            const dismissBtn = actionsContainer.querySelector('.dismiss-btn');
            if (dismissBtn) {
                dismissBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    card.remove();
                    checkShelfEmpty();
                });
            }
        }
    } else {
        card.classList.add('interrupted');
        if (progressTrack) progressTrack.style.display = 'none';
        if (statusSpan) statusSpan.textContent = 'Failed';
        if (actionsContainer) {
            actionsContainer.innerHTML = `
                <button class="download-action-btn dismiss-btn" title="Dismiss"><i class="fa-solid fa-xmark"></i></button>
            `;
            const dismissBtn = actionsContainer.querySelector('.dismiss-btn');
            if (dismissBtn) {
                dismissBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    card.remove();
                    checkShelfEmpty();
                });
            }
        }
    }
}

export function initDownloadShelf() {
    const clearBtn = document.getElementById('ClearDownloadsBtn');
    const closeBtn = document.getElementById('CloseDownloadsBtn');

    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            clearCompletedDownloads();
        });
    }

    if (closeBtn) {
        closeBtn.addEventListener('click', () => {
            hideDownloadShelf();
        });
    }

    if (window.flyAPI) {
        if (typeof window.flyAPI.onDownloadStarted === 'function') {
            window.flyAPI.onDownloadStarted((data) => handleDownloadStarted(data));
        }
        if (typeof window.flyAPI.onDownloadProgress === 'function') {
            window.flyAPI.onDownloadProgress((data) => handleDownloadProgress(data));
        }
        if (typeof window.flyAPI.onDownloadDone === 'function') {
            window.flyAPI.onDownloadDone((data) => handleDownloadDone(data));
        }
    }
}
