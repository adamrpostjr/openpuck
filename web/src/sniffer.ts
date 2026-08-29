import { mount } from 'svelte';
import './app.css';
import Sniffer from './Sniffer.svelte';

export default mount(Sniffer, { target: document.getElementById('app')! });
