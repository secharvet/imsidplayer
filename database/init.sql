-- Initialisation complète de la base de données imSidPlayer

-- Enable UUID extension
create extension if not exists "uuid-ossp";

-- ==========================================
-- TABLE: account_transfer
-- ==========================================
CREATE TABLE IF NOT EXISTS public.account_transfer (
  code TEXT PRIMARY KEY,              -- Code de récupération (8 chiffres)
  refresh_token TEXT NOT NULL,       -- Le refresh_token à transférer
  username TEXT UNIQUE NOT NULL,     -- Username logique (unique)
  created_at TIMESTAMPTZ DEFAULT now() NOT NULL,
  expires_at TIMESTAMPTZ DEFAULT (now() + INTERVAL '24 hours') NOT NULL
);

-- Index
CREATE INDEX IF NOT EXISTS idx_account_transfer_code ON public.account_transfer(code);
CREATE INDEX IF NOT EXISTS idx_account_transfer_username ON public.account_transfer(username);

-- RLS
ALTER TABLE public.account_transfer ENABLE ROW LEVEL SECURITY;

-- Politiques
CREATE POLICY "Users can create recovery codes" ON public.account_transfer 
  FOR INSERT WITH CHECK (auth.uid() IS NOT NULL);

CREATE POLICY "Anyone can read non-expired codes" ON public.account_transfer 
  FOR SELECT USING (expires_at > now());

CREATE POLICY "Users can delete their own codes" ON public.account_transfer 
  FOR DELETE USING (auth.uid() IS NOT NULL);

-- ==========================================
-- TABLE: community_ratings
-- ==========================================
CREATE TABLE IF NOT EXISTS public.community_ratings (
    id uuid DEFAULT uuid_generate_v4() PRIMARY KEY,
    user_id uuid REFERENCES auth.users NOT NULL,
    file_hash text NOT NULL,
    rating integer NOT NULL CHECK (rating >= 1 AND rating <= 5),
    created_at timestamp with time zone DEFAULT timezone('utc'::text, now()) NOT NULL,
    updated_at timestamp with time zone DEFAULT timezone('utc'::text, now()) NOT NULL,
    UNIQUE (user_id, file_hash)
);

-- Index
CREATE INDEX IF NOT EXISTS idx_community_ratings_file_hash ON public.community_ratings(file_hash);

-- RLS
ALTER TABLE public.community_ratings ENABLE ROW LEVEL SECURITY;

-- Tout le monde peut lire les ratings
CREATE POLICY "Public ratings are viewable by everyone." ON public.community_ratings 
  FOR SELECT USING (true);

-- Les utilisateurs authentifiés peuvent gérer leurs propres ratings
CREATE POLICY "Users can insert their own ratings." ON public.community_ratings 
  FOR INSERT WITH CHECK (auth.uid() = user_id);

CREATE POLICY "Users can update their own ratings." ON public.community_ratings 
  FOR UPDATE USING (auth.uid() = user_id);

CREATE POLICY "Users can delete their own ratings." ON public.community_ratings 
  FOR DELETE USING (auth.uid() = user_id);
