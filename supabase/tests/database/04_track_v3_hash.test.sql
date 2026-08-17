begin;
create extension if not exists pgtap with schema extensions;

select plan(2);

select is(
  private.base64url_encode(extensions.digest(
    private.track_v3_point_bytes('[468123456,-740123456,1786641000,125,9,7]'::jsonb) ||
    private.track_v3_point_bytes('[468123500,-740123400,1786641005,140,9,7]'::jsonb) ||
    private.track_v3_point_bytes('[468123500,-740123400,1786641065,0,9,13]'::jsonb),
    'sha256'
  )),
  '_jihWuBz7CuQ7kZODIzPxZJZIgUGDhufAOdLFvZNFlc',
  'database Track v3 codec reproduces the frozen little-endian fixture hash'
);

select ok(
  not has_function_privilege('service_role', 'private.track_v3_point_bytes(jsonb)', 'execute'),
  'the internal Track v3 codec is not an exposed RPC surface'
);

select * from finish();
rollback;
