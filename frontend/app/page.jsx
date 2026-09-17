import Link from 'next/link';
import ControlsPanel from '../components/ControlsPanel';
import LiveView from '../components/LiveView';

export default function Home() {
  return <main className="shell"><header className="topbar"><div><p className="eyebrow">EYES / PERCEPTION LAB</p><h1>XIAO Cam <span>Live</span></h1></div><Link href="/log" className="nav-link">Session archive <span>↗</span></Link></header><div className="live-layout"><div><LiveView /><ControlsPanel /></div><aside className="side-note"><p className="eyebrow">CAPTURE NODE 01</p><p className="large-note">Observe the room<br />in real time.</p><p className="muted">Every frame is relayed live and buffered locally if the uplink slows.</p></aside></div></main>;
}