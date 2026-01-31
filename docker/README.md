# Scripts Docker

Ce dossier contient tous les scripts et fichiers Docker pour la compilation Windows.

## Fichiers

- **`Dockerfile.windows`** : Image Docker pour la compilation croisée Windows (MinGW)
- **`docker-build-windows.sh`** : Construire l'image Docker
- **`docker-run-windows.sh`** : Lancer un shell interactif dans le conteneur
- **`docker-test-windows.sh`** : Tester la compilation complète
- **`docker-push-ghcr.sh`** : Pousser l'image vers GitHub Container Registry
- **`docker-setup.sh`** : Installer Podman/Docker (nécessite sudo)

## Utilisation

Tous les scripts doivent être exécutés depuis la racine du projet :

```bash
# Depuis la racine du projet
./docker/docker-build-windows.sh
./docker/docker-run-windows.sh
./docker/docker-test-windows.sh
```

Les scripts se placent automatiquement à la racine du projet avant d'exécuter les commandes Docker.

## Documentation complète

Voir `README.DOCKER.md` à la racine du projet pour la documentation complète.
