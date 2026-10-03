-- Owner-visible durable deletion status survives loss of dog membership.
create or replace function api.list_my_deletion_jobs_v1()
returns jsonb
language plpgsql stable security definer set search_path = ''
as $$
begin
  if auth.uid() is null or not exists (
    select 1 from auth.users where id = auth.uid() and deleted_at is null
  ) then
    raise exception using errcode = '28000', message = 'authentication_required';
  end if;
  return coalesce((select jsonb_agg(private.deletion_job_result_v1(recent.id) order by recent.requested_at desc)
    from (select job.id, job.requested_at
      from private.deletion_jobs job join private.deletion_tombstones t on t.id = job.tombstone_id
      where t.requested_by_sha256 = extensions.digest(auth.uid()::text, 'sha256')
      order by job.requested_at desc, job.id desc limit 100) recent), '[]'::jsonb);
end
$$;
revoke all on function api.list_my_deletion_jobs_v1() from public, anon, authenticated;
grant execute on function api.list_my_deletion_jobs_v1() to authenticated;

create or replace function api.retry_my_deletion_job_v1(p_job_id uuid)
returns jsonb
language plpgsql security definer set search_path = ''
as $$
begin
  -- Uses the same live-identity and requester binding as the existing status RPC.
  perform api.get_deletion_job_v1(p_job_id);
  update private.deletion_jobs set status = 'pending', last_error_code = null,
    next_attempt_at = statement_timestamp()
  where id = p_job_id and status = 'failed';
  return api.get_deletion_job_v1(p_job_id);
end
$$;
revoke all on function api.retry_my_deletion_job_v1(uuid) from public, anon, authenticated;
grant execute on function api.retry_my_deletion_job_v1(uuid) to authenticated;
