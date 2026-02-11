-- Politique UPDATE pour account_transfer
-- Après recovery, le nouveau refresh_token (rotation Supabase) doit être mis à jour dans la table

GRANT UPDATE ON public.account_transfer TO authenticated;

CREATE POLICY "Authenticated users can update recovery codes" ON public.account_transfer 
  FOR UPDATE USING (auth.uid() IS NOT NULL);
