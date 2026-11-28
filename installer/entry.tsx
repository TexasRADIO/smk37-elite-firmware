import React from 'react';
import { createRoot } from 'react-dom/client';
import Installer, { ReturnToMvave } from './EliteInstaller';

const container = document.getElementById('app')!;
const view = container.dataset.mode === 'return' ? <ReturnToMvave /> : <Installer />;
createRoot(container).render(view);
