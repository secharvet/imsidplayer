#!/usr/bin/env python3
"""
Script de migration des ratings de history.json vers rating.json

Ce script :
1. Lit le fichier history.json depuis le répertoire de configuration
2. Extrait tous les ratings (metadataHash -> rating) où rating > 0
3. Crée un nouveau fichier rating.json avec ces données
4. Affiche un résumé de la migration

Usage:
    python3 migrate_ratings.py [--config-dir PATH] [--dry-run]
"""

import json
import os
import sys
import argparse
from pathlib import Path
from typing import Dict, List, Tuple


def get_config_dir() -> Path:
    """
    Détermine le répertoire de configuration selon la plateforme.
    Compatible Windows et Unix/Linux.
    """
    if sys.platform == "win32":
        # Windows: utiliser APPDATA ou USERPROFILE
        appdata = os.getenv("APPDATA")
        if appdata:
            home_dir = Path(appdata)
        else:
            userprofile = os.getenv("USERPROFILE")
            if userprofile:
                home_dir = Path(userprofile)
            else:
                # Fallback: répertoire courant
                return Path.cwd() / ".imsidplayer"
    else:
        # Unix/Linux: utiliser HOME
        home = os.getenv("HOME")
        if not home:
            # Fallback: répertoire courant
            return Path.cwd() / ".imsidplayer"
        home_dir = Path(home)
    
    config_dir = home_dir / ".imsidplayer"
    return config_dir


def load_history_json(history_path: Path) -> List[Dict]:
    """
    Charge le fichier history.json et retourne la liste des entrées.
    Essaie plusieurs encodages en cas d'échec.
    """
    if not history_path.exists():
        print(f"⚠️  Le fichier {history_path} n'existe pas.")
        return []
    
    # Essayer plusieurs encodages
    encodings = ['utf-8', 'latin-1', 'windows-1252', 'cp1252', 'iso-8859-1']
    
    for encoding in encodings:
        try:
            with open(history_path, 'r', encoding=encoding) as f:
                data = json.load(f)
            
            # Le fichier peut être soit une liste directement, soit un objet avec une clé "entries"
            if isinstance(data, list):
                if encoding != 'utf-8':
                    print(f"✅ Fichier lu avec l'encodage {encoding}")
                return data
            elif isinstance(data, dict) and "entries" in data:
                if encoding != 'utf-8':
                    print(f"✅ Fichier lu avec l'encodage {encoding}")
                return data["entries"]
            else:
                print(f"⚠️  Format inattendu dans {history_path}")
                return []
        except UnicodeDecodeError:
            # Essayer le prochain encodage
            continue
        except json.JSONDecodeError as e:
            print(f"❌ Erreur de parsing JSON dans {history_path}: {e}")
            return []
        except Exception as e:
            # Si c'est la dernière tentative, afficher l'erreur
            if encoding == encodings[-1]:
                print(f"❌ Erreur lors de la lecture de {history_path}: {e}")
            continue
    
    print(f"❌ Impossible de lire {history_path} avec les encodages testés: {', '.join(encodings)}")
    return []


def extract_ratings(entries: List[Dict]) -> Dict[int, int]:
    """
    Extrait les ratings depuis les entrées d'historique.
    Retourne un dictionnaire metadataHash -> rating (seulement pour rating > 0).
    """
    ratings = {}
    
    for entry in entries:
        # Vérifier que l'entrée a les champs nécessaires
        if "metadataHash" not in entry or "rating" not in entry:
            continue
        
        metadata_hash = entry["metadataHash"]
        rating = entry["rating"]
        
        # Convertir en int si nécessaire
        try:
            metadata_hash = int(metadata_hash)
            rating = int(rating)
        except (ValueError, TypeError):
            continue
        
        # Ne garder que les ratings > 0
        if rating > 0:
            # Si plusieurs entrées ont le même hash, garder le rating le plus élevé
            if metadata_hash not in ratings or rating > ratings[metadata_hash]:
                ratings[metadata_hash] = rating
    
    return ratings


def create_rating_json(ratings: Dict[int, int]) -> Dict:
    """
    Crée la structure JSON pour rating.json.
    Format: {"ratings": [{"metadataHash": hash, "rating": rating}, ...]}
    """
    rating_entries = [
        {"metadataHash": hash_val, "rating": rating}
        for hash_val, rating in sorted(ratings.items())
    ]
    
    return {"ratings": rating_entries}


def save_rating_json(rating_path: Path, data: Dict, dry_run: bool = False) -> bool:
    """
    Sauvegarde les données dans rating.json.
    """
    if dry_run:
        print(f"[DRY-RUN] Écriture dans {rating_path}")
        print(json.dumps(data, indent=2, ensure_ascii=False))
        return True
    
    try:
        # Créer le répertoire parent si nécessaire
        rating_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Sauvegarder avec indentation pour lisibilité
        with open(rating_path, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        
        return True
    except Exception as e:
        print(f"❌ Erreur lors de l'écriture de {rating_path}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Migre les ratings de history.json vers rating.json"
    )
    parser.add_argument(
        "--config-dir",
        type=str,
        help="Répertoire de configuration (par défaut: ~/.imsidplayer ou %APPDATA%/.imsidplayer)"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Affiche ce qui serait fait sans modifier les fichiers"
    )
    
    args = parser.parse_args()
    
    # Déterminer le répertoire de configuration
    if args.config_dir:
        config_dir = Path(args.config_dir)
    else:
        config_dir = get_config_dir()
    
    print(f"📁 Répertoire de configuration: {config_dir}")
    
    # Chemins des fichiers
    history_path = config_dir / "history.json"
    rating_path = config_dir / "rating.json"
    
    # Charger history.json
    print(f"\n📖 Lecture de {history_path}...")
    entries = load_history_json(history_path)
    
    if not entries:
        print("❌ Aucune entrée trouvée dans history.json. Migration annulée.")
        return 1
    
    print(f"✅ {len(entries)} entrée(s) trouvée(s) dans history.json")
    
    # Extraire les ratings
    print(f"\n🔍 Extraction des ratings...")
    ratings = extract_ratings(entries)
    
    if not ratings:
        print("⚠️  Aucun rating > 0 trouvé dans history.json. Migration annulée.")
        return 0
    
    print(f"✅ {len(ratings)} rating(s) trouvé(s) (rating > 0)")
    
    # Afficher quelques statistiques
    rating_counts = {}
    for rating in ratings.values():
        rating_counts[rating] = rating_counts.get(rating, 0) + 1
    
    print(f"\n📊 Répartition des ratings:")
    for rating in sorted(rating_counts.keys(), reverse=True):
        count = rating_counts[rating]
        stars = "⭐" * rating
        print(f"   {stars} ({rating} étoiles): {count} morceau(x)")
    
    # Créer la structure pour rating.json
    rating_data = create_rating_json(ratings)
    
    # Vérifier si rating.json existe déjà
    if rating_path.exists() and not args.dry_run:
        print(f"\n⚠️  Le fichier {rating_path} existe déjà.")
        response = input("Voulez-vous le remplacer? (o/N): ")
        if response.lower() not in ['o', 'oui', 'y', 'yes']:
            print("❌ Migration annulée.")
            return 1
    
    # Sauvegarder rating.json
    print(f"\n💾 Sauvegarde dans {rating_path}...")
    if save_rating_json(rating_path, rating_data, args.dry_run):
        if args.dry_run:
            print("✅ [DRY-RUN] Migration simulée avec succès!")
        else:
            print("✅ Migration terminée avec succès!")
            print(f"\n📝 Résumé:")
            print(f"   - Fichier source: {history_path}")
            print(f"   - Fichier destination: {rating_path}")
            print(f"   - Ratings migrés: {len(ratings)}")
            print(f"\n💡 Prochaine étape: Modifier le code C++ pour utiliser rating.json au lieu de history.json")
        return 0
    else:
        print("❌ Échec de la sauvegarde.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
