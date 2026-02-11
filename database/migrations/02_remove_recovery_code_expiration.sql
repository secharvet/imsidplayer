-- Suppression de l'expiration des codes de récupération
-- Les codes ne doivent jamais expirer ; seule la régénération manuelle les invalide

-- 1. Supprimer d'abord la politique qui dépend de expires_at
DROP POLICY IF EXISTS "Anyone can read non-expired codes" ON public.account_transfer;

-- 2. Supprimer la colonne expires_at
ALTER TABLE public.account_transfer DROP COLUMN IF EXISTS expires_at;

-- 3. Nouvelle politique : lecture sans filtre d'expiration
CREATE POLICY "Anyone can read recovery codes" ON public.account_transfer 
  FOR SELECT USING (true);
