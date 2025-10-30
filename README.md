# 🚀 Azure Arc Agent Checker

![C++](https://img.shields.io/badge/language-C++-blue?style=flat-square) ![License](https://img.shields.io/badge/license-MIT-green?style=flat-square) ![Maintained](https://img.shields.io/badge/Maintained%3F-yes-brightgreen?style=flat-square) ![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square)

## 📑 Table des Matières

- [📋 Description](#-description)
- [✨ Fonctionnalités](#-fonctionnalités)
- [Compilation](#compilation)
- [🚀 Utilisation](#-utilisation)
- [Architecture Technique](#architecture-technique)
- [Logging](#logging)
- [🚀 Cas d'Usage](#-cas-dusage)
- [Limitations](#limitations)
- [Exemple Output](#exemple-output)
- [🔧 Dépannage](#-dépannage)
- [📄 Licence](#-licence)
- [Support](#support)




**Version:** 1.0
**Auteur:** Ayi NEDJIMI Consultants
**Date:** 2025

## 📋 Description

Outil de diagnostic pour Azure Arc Agent permettant de vérifier l'état de l'agent, l'expiration des tokens d'authentification, et les extensions installées sur un serveur Azure Arc-enabled.


## ✨ Fonctionnalités

### 1. Vérification Agent
- Détection des processus `himds.exe` et `azcmagent.exe`
- Lecture de la configuration dans `C:\ProgramData\AzureConnectedMachineAgent\Config\agentconfig.json`
- Extraction du Resource ID, Location, Tenant ID
- Vérification de l'expiration du token (sans exposer le token complet)
- Interrogation du Event Log `Microsoft-AzureArc-Agent/Operational`

### 2. Énumération Extensions
- Liste toutes les extensions Azure installées dans `C:\Packages\Plugins\Microsoft.Azure.*`
- Affichage du nom et du statut de chaque extension
- Détection des extensions sans fichier de status

### 3. Alertes
- Token expiré ou proche de l'expiration (< 24h)
- Processus critiques non démarrés
- Erreurs récentes dans le Event Log
- Configuration manquante

### 4. Export CSV
- Export complet avec colonnes: Composant, État, Version, Expiration, Détails, Alertes
- Format UTF-8 avec BOM


## Compilation

### Prérequis
- Visual Studio 2019/2022 avec MSVC
- Windows SDK 10.0 ou supérieur
- Droits administrateur recommandés pour accès complet

### Build
```batch
go.bat
```

Ou manuellement:
```batch
cl.exe /O2 /EHsc /D_UNICODE /DUNICODE AzureArcAgentChecker.cpp ^
  /link comctl32.lib psapi.lib wevtapi.lib advapi32.lib user32.lib gdi32.lib shell32.lib
```


## 🚀 Utilisation

### Lancement
```batch
AzureArcAgentChecker.exe
```

**Note:** Nécessite des privilèges élevés pour accès complet aux configurations et Event Log.

### Interface

#### Boutons
- **Vérifier Agent** : Analyse complète des composants Arc (processus, config, tokens, events)
- **Lister Extensions** : Énumère toutes les extensions Azure installées
- **Exporter CSV** : Sauvegarde les résultats dans un fichier CSV

#### Colonnes ListView
- **Composant** : Nom du composant Arc vérifié
- **État** : État actuel (Actif, Non trouvé, etc.)
- **Version/Chemin** : Chemin du processus ou version détectée
- **Expiration Token** : Timestamp d'expiration (si disponible)
- **Détails** : Informations supplémentaires (PID, Resource ID, etc.)
- **Alertes** : Avertissements ou erreurs détectées


## Architecture Technique

### APIs Utilisées
- **psapi.lib** : Énumération des processus (CreateToolhelp32Snapshot)
- **wevtapi.lib** : Requêtes Event Log (EvtQuery)
- **advapi32.lib** : Accès registre et sécurité
- **comctl32.lib** : ListView et contrôles UI

### Fichiers Analysés
- `C:\ProgramData\AzureConnectedMachineAgent\Config\agentconfig.json`
- `C:\ProgramData\AzureConnectedMachineAgent\Tokens\metadata.json`
- `C:\Packages\Plugins\Microsoft.Azure.*\`

### Sécurité
- **Aucun token complet n'est affiché** - Seul le timestamp d'expiration est extrait
- Pas d'envoi de données sur le réseau
- Lecture seule des configurations


## Logging

Les logs sont stockés dans:
```
%TEMP%\WinTools_AzureArcAgentChecker_log.txt
```

Format: Timestamp + message texte


## 🚀 Cas d'Usage

### 1. Diagnostic Connectivité Arc
Vérifier que tous les composants Arc sont actifs et correctement configurés.

### 2. Audit Sécurité
Valider l'expiration des tokens et détecter des accès anormaux via Event Log.

### 3. Inventaire Extensions
Lister toutes les extensions déployées pour audit de conformité.

### 4. Troubleshooting
Identifier rapidement pourquoi un serveur n'apparaît plus dans Azure Arc.


## Limitations

- Parser JSON simplifié (recherche de clés basique)
- Event Log uniquement si canal `Microsoft-AzureArc-Agent/Operational` existe
- Nécessite que l'agent Arc soit installé pour trouver les fichiers


## Exemple Output

```
Composant             | État                | Version              | Expiration       | Détails
- ---------------------|---------------------|----------------------|------------------|------------------
Service HIMDS         | En cours d'exécution| PID: 1234            |                  | Actif
Agent Azure Arc       | En cours d'exécution| PID: 5678            |                  | Actif
Configuration Agent   | Configuration trouvée|                     | 1735689600       | Resource: /subscriptions/...
Extension             | Installée           | Microsoft.Azure.Monitor | Status présent |
Event Log             | Event récent        |                      |                  | Avertissement détecté
```


## 🔧 Dépannage

### Erreur "Fichier config manquant"
- Vérifier que l'agent Arc est installé
- Vérifier les permissions sur `C:\ProgramData\AzureConnectedMachineAgent\`

### "Processus non démarré"
- Vérifier le service "Hybrid Instance Metadata Service" dans services.msc
- Redémarrer le service si nécessaire

### Event Log inaccessible
- Lancer l'outil en tant qu'Administrateur
- Vérifier que le canal Event Log existe


## 📄 Licence

(c) 2025 Ayi NEDJIMI Consultants - Tous droits réservés


## Support

Pour questions ou support: contact@ayinedjimi-consultants.com


- --

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>

- --

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>

- --

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>

- --

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>

- --

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>

---

<div align="center">

**⭐ Si ce projet vous plaît, n'oubliez pas de lui donner une étoile ! ⭐**

</div>