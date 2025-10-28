# Projet HydroControl-GE V2.0

## 1. Vision et Objectif

**HydroControl-GE** est un système d'Internet des Objets (IoT) industriel conçu pour l'automatisation et la supervision intelligente de la gestion hydraulique. Son objectif principal est d'assurer une distribution autonome et optimisée de l'eau entre un parc de puits et de réservoirs, typiquement au sein de complexes sportifs ou d'installations agricoles.

Le système est pensé pour être :
- **Robuste** : Grâce à des communications fiables et des mécanismes de redondance.
- **Sécurisé** : Avec un chiffrement de bout en bout des communications.
- **Facile à déployer** : Grâce à un système de configuration initiale simplifié.

## 2. Architecture Générale

Le système s'articule autour d'une topologie réseau en étoile avec des capacités de communication directe (peer-to-peer) pour la redondance.

- **Centrale HydroControl-GE** : Le coordinateur du réseau. Elle sert de passerelle LoRa/Wi-Fi, d'orchestrateur, et héberge l'interface de supervision globale.
- **Modules AquaReservPro** : Des nœuds intelligents qui mesurent le niveau d'eau d'un réservoir et pilotent la logique de commande de la pompe associée.
- **Modules WellguardPro** : Des actionneurs qui reçoivent les ordres des `AquaReservPro` et commandent physiquement les pompes des puits via des relais de puissance.

*(Un schéma détaillé de l'architecture sera ajouté dans une version future de la documentation.)*

## 3. Protocole de Communication

La communication entre les modules est un élément clé du système. Elle s'appuie sur le protocole LoRa sur la bande 433 MHz pour sa longue portée et sa faible consommation.

### 3.1. Sécurité

Toutes les communications LoRa sont chiffrées en **AES-128**. Une clé secrète partagée, configurée lors de l'installation de chaque module, est utilisée pour dériver la clé de chiffrement, garantissant que seuls les modules autorisés peuvent communiquer.

### 3.2. Découverte et Appairage

Au démarrage, chaque nœud (`AquaReservPro`, `WellguardPro`) envoie un paquet de **découverte** sur le réseau. La `Centrale` écoute ces paquets, enregistre l'identité unique de chaque nœud (basée sur son adresse MAC) et lui renvoie un accusé de réception (`WELCOME_ACK`).

### 3.3. Fiabilité des Commandes

Pour garantir que les commandes de pompe sont bien exécutées, le protocole intègre un mécanisme de fiabilité :
1.  Un `AquaReservPro` envoie une commande à un `WellguardPro`.
2.  Il attend un **acquittement (ACK)** en retour.
3.  En cas d'échec après plusieurs tentatives, il envoie une **demande de relais** à la `Centrale`.
4.  La `Centrale` relaie alors la commande au `WellguardPro`, offrant ainsi un chemin de communication alternatif.

## 4. Déploiement

Chaque module matériel, lors de son premier démarrage, crée un point d'accès Wi-Fi. L'installateur peut s'y connecter via un smartphone ou un ordinateur pour configurer les identifiants du réseau Wi-Fi local et la clé secrète LoRa partagée. Une fois configuré, le module redémarre et se connecte automatiquement au réseau principal.
