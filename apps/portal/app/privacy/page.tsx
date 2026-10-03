import Link from "next/link";
import type { Metadata } from "next";

export const metadata: Metadata = { title: "Privacidad | Dog RGB" };

export default function PrivacyPage() {
  return <main className="onboarding-content" id="main-content">
    <Link href="/">DOG-RGB_</Link><h1>Privacidad y tus datos</h1>
    <p>La nube es opcional. El collar puede seguir usando sus funciones locales sin una cuenta web.</p>
    <h2>Qué guarda el portal</h2>
    <p>Correo y sesión de acceso; nombre y zona horaria del perro; identidad y estado del collar; configuración; posiciones GPS, tiempos, velocidad y calidad de las grabaciones; y resúmenes calculados. Las ubicaciones pueden revelar recorridos de personas: vincula un collar solo con su autorización.</p>
    <h2>Para qué y dónde</h2>
    <p>Usamos estos datos para mostrar historial y actividad, sincronizar el collar y recuperar tu cuenta. Supabase gestiona autenticación y base de datos; el alojamiento web procesa las solicitudes. En desarrollo estos servicios pueden ejecutarse en tu equipo. No hay publicidad ni publicación de tus recorridos.</p>
    <h2>Acceso y retención</h2>
    <p>El acceso depende de la pertenencia al perro. Un propietario puede exportar, revocar el collar o eliminar datos para todos sus miembros. La política prevista conserva telemetría cruda durante 12 meses. El borrado periódico requiere activar y verificar el proceso de retención en cada instalación; este portal no afirma que esté activo en todas ellas.</p>
    <h2>Revocar, exportar y borrar</h2>
    <p>Revocar corta futuras sincronizaciones, pero conserva el historial. Eliminar bloquea el acceso al aceptar la solicitud y purga los registros mediante un proceso por lotes. Puedes consultar su estado desde Cuenta aunque el perro ya no aparezca.</p>
    <p>Eliminar la cuenta incluye los perros que posees y afecta a todos sus miembros. Primero se purgan sus datos; la identidad de acceso se conserva para consultar y reintentar trabajos pendientes, y se elimina al finalizar.</p>
    <p>El borrado de datos activos no elimina inmediatamente las copias de seguridad. Su caducidad depende del alojamiento; antes de publicar una instalación deben fijarse el plazo de copias y el procedimiento que reaplica los borrados al restaurar.</p>
    <p><Link href="/account">Gestionar cuenta y datos</Link> · <Link href="/onboarding">Abrir portal</Link></p>
  </main>;
}
