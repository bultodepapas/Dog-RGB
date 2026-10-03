import Link from "next/link";
import { AccountDeletionPanel } from "../components/account-deletion-panel";
import { accountDeletion, accountPreview } from "../../lib/privacy/account";
import type { Metadata } from "next";
import { requireFreshPageIdentity } from "../../lib/auth/route-guard";
import { listDogSummaries } from "../../lib/data-access/dogs";
import { parseDeletionJobs } from "../../lib/privacy/deletion";
import { createServerSupabaseClient } from "../../lib/supabase/server";
import { OnboardingPrivateShell } from "../components/private-shell";
import { retryDeletionAction } from "./actions";

export const metadata: Metadata = { title: "Cuenta y datos | Dog RGB" };
export const dynamic = "force-dynamic";
const STATUSES = { pending: "Pendiente", processing: "Procesando", failed: "Necesita reintento", completed: "Borrado completado" };
const ROLES = { owner: "propietario", editor: "editor", viewer: "lector" };

export default async function AccountPage() {
  await requireFreshPageIdentity("/account");
  const client = await createServerSupabaseClient();
  const [dogs, jobsResult, accountResult] = await Promise.all([listDogSummaries(), client.rpc("list_my_deletion_jobs_v1"), client.rpc("get_my_account_deletion_v1")]);
  if (jobsResult.error) throw new Error("No pudimos consultar tus solicitudes. Recarga la página.");
  const jobs = parseDeletionJobs(jobsResult.data);
  if (accountResult.error) throw new Error("No pudimos consultar el estado de tu cuenta.");
  const deletion = accountDeletion(accountResult.data);
  const previewResult = deletion.status === "pending" || deletion.status === "ready" ? null : await client.rpc("preview_my_account_deletion_v1");
  if (previewResult?.error) throw new Error("No pudimos cargar el inventario de tu cuenta.");
  const preview = previewResult ? accountPreview(previewResult.data) : null;
  return <OnboardingPrivateShell>
    <h1>Cuenta y datos</h1>
    <p><Link href="/onboarding">Volver a mis perros</Link> · <Link href="/privacy">Privacidad</Link></p>
    <section aria-labelledby="account-dogs"><h2 id="account-dogs">Perros vinculados</h2>
      {dogs.length ? <ul>{dogs.map(dog => <li key={dog.id}><Link href={`/app/${dog.id}/data`}>{dog.name}: datos y exportación</Link> ({ROLES[dog.role]})</li>)}</ul> : <p>No tienes perros disponibles.</p>}
    </section>
    <AccountDeletionPanel preview={preview} deletion={deletion} />
    <section aria-labelledby="account-deletions"><h2 id="account-deletions">Solicitudes de borrado</h2>
      <p>Se muestran hasta 100 solicitudes recientes. El cierre de acceso es inmediato. El proceso de purga debe estar activo para completar un borrado. Las copias de seguridad caducan por separado.</p>
      <a className="button-link" href="/account">Actualizar estado</a>
      {jobs.length ? <ul>{jobs.map(job => <li key={job.jobId}>
        <strong>{STATUSES[job.status]}</strong><p>Perro: {job.dogId}</p><p>Solicitud: {job.jobId}</p>
        <p>Solicitado: <time dateTime={job.requestedAt}>{job.requestedAt}</time></p>
        {job.status === "failed" ? <form action={retryDeletionAction}><input name="jobId" type="hidden" value={job.jobId} /><button type="submit">Reintentar borrado</button></form> : null}
      </li>)}</ul> : <p>No hay solicitudes.</p>}
    </section>
  </OnboardingPrivateShell>;
}
