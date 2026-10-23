-- Hoca Nureddin'in anlattığı gizli portre: bir ipucu daha.
-- rowl.var_get / rowl.var_set metin değerleriyle çalışır; "add" ile üretilen
-- sayılar "1.000000" biçiminde gelir, bu yüzden tonumber kullanılır.
function on_enter()
  local n = tonumber(rowl.var_get("ipucu")) or 0
  rowl.var_set("ipucu", string.format("%d", n + 1))
end
