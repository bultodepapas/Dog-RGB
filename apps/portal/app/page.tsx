import { DEVICE_PROTOCOL } from "@dog-rgb/contracts";

export default function Home() {
  return (
    <main>
      <p className="eyebrow">DOG-RGB_ CLOUD FOUNDATION</p>
      <h1>El collar sigue funcionando sin la nube.</h1>
      <p>
        Scaffold local de Phase 1 listo para Auth, pairing y sincronización
        transaccional. La interfaz de producto se implementa en fases posteriores.
      </p>
      <dl>
        <div><dt>PROTOCOLO</dt><dd>{DEVICE_PROTOCOL}</dd></div>
        <div><dt>ESTADO</dt><dd>LOCAL / OPTIONAL</dd></div>
      </dl>
    </main>
  );
}
