#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "FactoryMonitorSubsystem.generated.h"

/**
 * UFactoryMonitorSubsystem
 *
 * Sous-système "Plug & Play" d'export de métriques vers un backend SaaS.
 *
 * ARCHITECTURE :
 *  - UCLASS(Config=Game) → le token d'auth est auto-généré puis persisté
 *    dans Saved/Config/LinuxServer/Game.ini, section :
 *    [/Script/FactoryMonitor.FactoryMonitorSubsystem]
 *    L'utilisateur n'a RIEN à configurer dans les fichiers du mod.
 *
 * OPTIMISATION TPS :
 *  - Pas de Tick() ni de FTickableGameObject.
 *    Un FTimerHandle déclenche la collecte toutes les 60 secondes,
 *    soit ~0,016 % du temps CPU total par rapport à un tick à 60 Hz.
 *  - Collecte via les registres natifs d'AFGBuildableSubsystem
 *    (parcours d'un TArray<AFGBuildable*> interne) au lieu de
 *    TActorIterator qui scanne le tableau global des UObject (~100 k+
 *    entrées sur un serveur chargé).
 *  - La requête HTTP est entièrement asynchrone (thread dédié UE HTTP).
 *    Le GameThread n'est pas bloqué pendant l'I/O réseau.
 *  - Un verrou bRequestInFlight empêche tout empilement de requêtes.
 */
UCLASS(Config=Game)
class FACTORYMONITOR_API UFactoryMonitorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Interface USubsystem
    // -----------------------------------------------------------------------

    /**
     * Instancie le sous-système UNIQUEMENT sur un serveur autoritaire.
     * Sur un client, retourner false évite toute instanciation et donc
     * tout risque de crash lié à des états serveur manquants.
     */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    /** Démarre le pipeline : token → timer. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Libère le timer proprement lors du teardown du monde. */
    virtual void Deinitialize() override;

private:
    // -----------------------------------------------------------------------
    // Configuration persistante — invisible pour l'utilisateur final.
    // Stockée dans Game.ini, générée automatiquement si absente.
    // -----------------------------------------------------------------------

    UPROPERTY(Config)
    FString ServerAuthToken;

    // -----------------------------------------------------------------------
    // État interne
    // -----------------------------------------------------------------------

    /** Handle du timer périodique (60 s). Nettoyé dans Deinitialize(). */
    FTimerHandle DataCollectionTimerHandle;

    /**
     * Verrou anti-empilement de requêtes.
     * Type bool simple — suffisant car tout s'exécute sur le GameThread.
     * Aucun besoin d'un std::atomic<bool> dans ce contexte single-thread.
     */
    bool bRequestInFlight = false;

    // -----------------------------------------------------------------------
    // Pipeline interne : Init → Collecte → Sérialisation → Envoi
    // -----------------------------------------------------------------------

    /** Génère ou charge le token, puis l'affiche dans les logs serveur. */
    void InitializeAuthToken();

    /**
     * Point d'entrée du timer.
     * Vérifie le verrou, puis lance la collecte et l'envoi.
     */
    void OnCollectionTimerFired();

    /**
     * Construit le snapshot complet en agrégeant les trois collecteurs.
     * @return Objet JSON racine, ou nullptr si le monde est invalide.
     */
    TSharedPtr<FJsonObject> BuildFullSnapshot() const;

    // Collecteurs par domaine — chacun retourne nullptr en cas d'échec
    // afin de ne pas interrompre les autres domaines.
    TSharedPtr<FJsonObject> CollectInventoryData()  const;
    TSharedPtr<FJsonObject> CollectProductionData() const;
    TSharedPtr<FJsonObject> CollectPowerData()      const;

    /**
     * Sérialise le payload JSON et lance la requête HTTP POST asynchrone.
     * Ne bloque jamais le GameThread (I/O délégué au thread HTTP d'UE).
     */
    void DispatchWebhookRequest(TSharedRef<FJsonObject> Payload);

    /**
     * Callback HTTP — s'exécute sur le GameThread grâce à BindWeakLambda.
     * Remet bRequestInFlight à false et journalise le résultat.
     * La requête est détruite automatiquement après ce callback
     * (le TSharedPtr HTTP tombe à zéro).
     */
    void OnHttpRequestComplete(FHttpRequestPtr  Request,
                               FHttpResponsePtr Response,
                               bool             bWasSuccessful);
};
