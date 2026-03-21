CREATE FUNCTION get_atomic_hit_ratio()
  RETURNS float8
  AS 'MODULE_PATHNAME', 'get_atomic_hit_ratio'
  LANGUAGE C STRICT;