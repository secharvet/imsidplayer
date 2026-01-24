#!/usr/bin/env python3
"""
Script pour supprimer les ratings de history.json

Ce script :
1. Lit le fichier history.json depuis le répertoire de configuration
2. Supprime le champ "rating" de toutes les entrées
3. Sauvegarde le fichier modifié
4. Affiche un résumé des modifications

Usage:
    python3 remove_ratings_from_history.py [--config-dir PATH] [--dry-run] [--backup]
"""

import json
import os
import sys
import argparse
import shutil
from pathlib import Path
from typing import List, Dict
from datetime import datetime


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


def remove_ratings_from_entries(entries: List[Dict]) -> tuple[List[Dict], int]:
    """
    Supprime le champ "rating" de toutes les entrées.
    Retourne (entrées modifiées, nombre d'entrées avec rating supprimé).
    """
    modified_entries = []
    ratings_removed = 0
    
    for entry in entries:
        # Créer une copie de l'entrée sans le champ rating
        new_entry = {k: v for k, v in entry.items() if k != "rating"}
        
        # Compter si un rating a été supprimé
        if "rating" in entry:
            ratings_removed += 1
        
        modified_entries.append(new_entry)
    
    return modified_entries, ratings_removed


def save_history_json(history_path: Path, entries: List[Dict], dry_run: bool = False) -> bool:
    """
    Sauvegarde les entrées dans history.json.
    """
    if dry_run:
        print(f"[DRY-RUN] Écriture dans {history_path}")
        print(f"[DRY-RUN] {len(entries)} entrées à sauvegarder")
        # Afficher un exemple d'entrée
        if entries:
            print(f"[DRY-RUN] Exemple d'entrée (sans rating):")
            example = {k: v for k, v in entries[0].items() if k != "rating"}
            print(json.dumps(example, indent=2, ensure_ascii=False))
        return True
    
    try:
        # Sauvegarder avec indentation pour lisibilité
        with open(history_path, 'w', encoding='utf-8') as f:
            json.dump(entries, f, indent=2, ensure_ascii=False)
        
        return True
    except Exception as e:
        print(f"❌ Erreur lors de l'écriture de {history_path}: {e}")
        return False


def create_backup(history_path: Path) -> Path:
    """
    Crée une sauvegarde du fichier history.json.
    Retourne le chemin du fichier de sauvegarde.
    """
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = history_path.parent / f"history_backup_{timestamp}.json"
    
    try:
        shutil.copy2(history_path, backup_path)
        return backup_path
    except Exception as e:
        print(f"⚠️  Erreur lors de la création de la sauvegarde: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description="Supprime les ratings de history.json"
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
    parser.add_argument(
        "--backup",
        action="store_true",
        help="Crée une sauvegarde de history.json avant modification"
    )
    
    args = parser.parse_args()
    
    # Déterminer le répertoire de configuration
    if args.config_dir:
        config_dir = Path(args.config_dir)
    else:
        config_dir = get_config_dir()
    
    print(f"📁 Répertoire de configuration: {config_dir}")
    
    # Chemin du fichier
    history_path = config_dir / "history.json"
    
    # Charger history.json
    print(f"\n📖 Lecture de {history_path}...")
    entries = load_history_json(history_path)
    
    if not entries:
        print("❌ Aucune entrée trouvée dans history.json. Rien à faire.")
        return 1
    
    print(f"✅ {len(entries)} entrée(s) trouvée(s) dans history.json")
    
    # Compter les entrées avec rating
    entries_with_rating = sum(1 for entry in entries if "rating" in entry)
    print(f"📊 {entries_with_rating} entrée(s) avec un champ 'rating'")
    
    if entries_with_rating == 0:
        print("✅ Aucun rating à supprimer. Le fichier est déjà propre.")
        return 0
    
    # Créer une sauvegarde si demandé
    backup_path = None
    if args.backup and not args.dry_run:
        print(f"\n💾 Création d'une sauvegarde...")
        backup_path = create_backup(history_path)
        if backup_path:
            print(f"✅ Sauvegarde créée: {backup_path}")
        else:
            response = input("⚠️  Échec de la sauvegarde. Continuer quand même? (o/N): ")
            if response.lower() not in ['o', 'oui', 'y', 'yes']:
                print("❌ Opération annulée.")
                return 1
    
    # Supprimer les ratings
    print(f"\n🔧 Suppression des ratings...")
    modified_entries, ratings_removed = remove_ratings_from_entries(entries)
    
    print(f"✅ {ratings_removed} rating(s) supprimé(s)")
    
    # Vérifier que le fichier rating.json existe
    rating_path = config_dir / "rating.json"
    if not rating_path.exists():
        print(f"\n⚠️  Attention: Le fichier {rating_path} n'existe pas.")
        print("   Assurez-vous d'avoir exécuté migrate_ratings.py avant de supprimer les ratings de history.json.")
        if not args.dry_run:
            response = input("Continuer quand même? (o/N): ")
            if response.lower() not in ['o', 'oui', 'y', 'yes']:
                print("❌ Opération annulée.")
                return 1
    
    # Sauvegarder le fichier modifié
    print(f"\n💾 Sauvegarde de {history_path}...")
    if save_history_json(history_path, modified_entries, args.dry_run):
        if args.dry_run:
            print("✅ [DRY-RUN] Suppression simulée avec succès!")
        else:
            print("✅ Suppression terminée avec succès!")
            print(f"\n📝 Résumé:")
            print(f"   - Fichier modifié: {history_path}")
            if backup_path:
                print(f"   - Sauvegarde: {backup_path}")
            print(f"   - Entrées traitées: {len(entries)}")
            print(f"   - Ratings supprimés: {ratings_removed}")
            print(f"\n💡 Les ratings sont maintenant uniquement dans {rating_path}")
        return 0
    else:
        print("❌ Échec de la sauvegarde.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
