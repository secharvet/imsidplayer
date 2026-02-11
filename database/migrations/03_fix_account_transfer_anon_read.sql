-- Corriger l'accès en lecture pour le rôle anon (récupération de compte)
-- Sans GRANT, anon ne peut pas SELECT. La politique RLS filtre les lignes.

-- 1. GRANT explicite pour permettre à anon d'exécuter SELECT sur la table
GRANT SELECT ON public.account_transfer TO anon;

-- 2. S'assurer que la politique de lecture existe (re-créer si besoin)
DROP POLICY IF EXISTS "Anyone can read recovery codes" ON public.account_transfer;
DROP POLICY IF EXISTS "Anyone can read non-expired codes" ON public.account_transfer;

CREATE POLICY "Anyone can read recovery codes" ON public.account_transfer 
  FOR SELECT USING (true);
