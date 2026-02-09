-- Suppression de la colonne filepath de la table community_ratings
-- Cette colonne est redondante car le file_hash suffit pour l'identification

ALTER TABLE public.community_ratings DROP COLUMN IF EXISTS filepath;
