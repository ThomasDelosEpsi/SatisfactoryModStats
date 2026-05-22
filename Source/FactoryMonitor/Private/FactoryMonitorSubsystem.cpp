#include "FactoryMonitorSubsystem.h"

// ---------------------------------------------------------------------------
// Headers Satisfactory (CSS — Content Satisfactory Source)
// Ces inclusions ciblent les registres natifs du jeu pour une itération
// optimale. Chemins standards du SDK SML 3.11.3 / CSS UE 5.3.
// ---------------------------------------------------------------------------
#include "FGBuildableSubsystem.h"               // Registre central des bâtiments
#include "FGCircuitSubsystem.h"                 // Registre des circuits électriques
#include "FGPowerCircuit.h"                     // UFGPowerCircuit + FPowerCircuitStats
#include "FGCircuit.h"                          // UFGCircuit (classe de base)
#include "FGInventoryComponent.h"               // UFGInventoryComponent + FInventoryStack
#include "Buildables/FGBuildableStorage.h"      // Conteneurs de stockage
#include "Buildables/FGBuildableFactory.h"      // Base commune (IsProducing, GetCurrentPotential)
#include "Buildables/FGBuildableManufacturer.h" // Constructeurs, Fonderies, Assembleurs…
#include "Buildables/FGBuildableResourceExtractor.h" // Mineurs

// ---------------------------------------------------------------------------
// Headers Unreal Engine
// ---------------------------------------------------------------------------
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Guid.h"
#include "Misc/DateTime.h"
#include "TimerManager.h"
#include "Engine/World.h"
// Note : PAS d'EngineUtils.h (TActorIterator) — nous utilisons les registres
// natifs d'AFGBuildableSubsystem, plus rapides que TActorIterator.

DEFINE_LOG_CATEGORY_STATIC(LogFactoryMonitor, Log, All);

// ===========================================================================
// Constantes internes — jamais exposées à l'utilisateur final.
// Modifiez uniquement ici, puis recompilez.
// ===========================================================================
namespace FactoryMonitor
{
    /** URL du endpoint SaaS. Ne pas exposer dans une UPROPERTY. */
    static const FString kWebhookUrl     = TEXT("https://api.ton-domaine.com/webhook");

    /** Version du schéma JSON pour gérer les migrations côté backend. */
    static const FString kSchemaVersion  = TEXT("2.0");

    /** Intervalle en secondes entre chaque snapshot. */
    static constexpr float kPollInterval = 60.0f;

    /**
     * Délai avant le premier tick après l'initialisation du monde.
     * Laisse le serveur terminer le chargement des sous-systèmes natifs
     * (AFGBuildableSubsystem, AFGCircuitSubsystem…) avant la première collecte.
     */
    static constexpr float kInitialDelay = 20.0f;

    /** Timeout HTTP en secondes. Au-delà, bRequestInFlight est remis à false
     *  via le callback d'échec — aucune fuite mémoire possible. */
    static constexpr float kHttpTimeout  = 15.0f;
}

// ===========================================================================
// Interface USubsystem
// ===========================================================================

bool UFactoryMonitorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    if (!Super::ShouldCreateSubsystem(Outer))
    {
        return false;
    }

    const UWorld* World = Cast<UWorld>(Outer);
    if (!IsValid(World))
    {
        return false;
    }

    // Instanciation strictement réservée au serveur autoritaire.
    // NM_DedicatedServer = serveur dédié headless (notre cible principale).
    // NM_ListenServer    = hôte en co-op (a également l'autorité).
    // Les clients (NM_Client) sont explicitement exclus.
    const ENetMode NetMode = World->GetNetMode();
    return (NetMode == NM_DedicatedServer || NetMode == NM_ListenServer);
}

void UFactoryMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        UE_LOG(LogFactoryMonitor, Error,
            TEXT("[FactoryMonitor] Impossible d'initialiser : le World est invalide."));
        return;
    }

    // Étape 1 : charger ou générer le token d'authentification.
    InitializeAuthToken();

    // Étape 2 : armer le timer périodique.
    // bLoop = true  → le timer se réarme automatiquement après chaque exécution.
    // kInitialDelay → donne au serveur le temps de charger tous ses registres.
    World->GetTimerManager().SetTimer(
        DataCollectionTimerHandle,
        this,
        &UFactoryMonitorSubsystem::OnCollectionTimerFired,
        FactoryMonitor::kPollInterval,
        /*bLoop=*/true,
        /*FirstDelay=*/FactoryMonitor::kInitialDelay
    );

    UE_LOG(LogFactoryMonitor, Log,
        TEXT("[FactoryMonitor] Timer armé — premier tick dans %.0f s, puis toutes les %.0f s."),
        FactoryMonitor::kInitialDelay,
        FactoryMonitor::kPollInterval);
}

void UFactoryMonitorSubsystem::Deinitialize()
{
    // Nettoyage du timer — obligatoire pour éviter un appel de callback
    // sur un objet en cours de destruction (dangling pointer).
    UWorld* World = GetWorld();
    if (IsValid(World))
    {
        World->GetTimerManager().ClearTimer(DataCollectionTimerHandle);
    }

    Super::Deinitialize();
    UE_LOG(LogFactoryMonitor, Log, TEXT("[FactoryMonitor] Sous-système arrêté proprement."));
}

// ===========================================================================
// Gestion du Token d'Authentification
// ===========================================================================

void UFactoryMonitorSubsystem::InitializeAuthToken()
{
    // LoadConfig() force la lecture du fichier ini AVANT toute vérification.
    // Sur un serveur Linux dédié, le fichier cible est :
    //   <GameDir>/Saved/Config/LinuxServer/Game.ini
    // Section : [/Script/FactoryMonitor.FactoryMonitorSubsystem]
    // Clé     : ServerAuthToken=<valeur>
    LoadConfig();

    if (ServerAuthToken.IsEmpty())
    {
        // Première exécution ou token révoqué manuellement.
        // FGuid offre 128 bits d'entropie — suffisant pour un secret partagé.
        // DigitsWithHyphens → format standard : XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
        ServerAuthToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);

        // Persistance immédiate : écrit uniquement les UPROPERTY(Config)
        // de cette classe dans le fichier ini. Aucune autre valeur n'est altérée.
        SaveConfig();

        UE_LOG(LogFactoryMonitor, Log,
            TEXT("[FactoryMonitor] Nouveau token généré et sauvegardé dans Game.ini."));
    }

    // ---------------------------------------------------------------------------
    // Message d'administration très visible (niveau Warning = texte jaune dans
    // la plupart des viewers de logs, notamment journalctl et les consoles RCON).
    // Affiché à chaque démarrage du serveur pour que l'admin puisse toujours
    // retrouver son token sans ouvrir le fichier ini manuellement.
    // ---------------------------------------------------------------------------
    UE_LOG(LogFactoryMonitor, Warning, TEXT(""));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("================================================================"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("  FACTORY MONITOR — TOKEN D'AUTHENTIFICATION SERVEUR"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("----------------------------------------------------------------"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("  Token   : %s"), *ServerAuthToken);
    UE_LOG(LogFactoryMonitor, Warning, TEXT("----------------------------------------------------------------"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("  → Copie ce token dans ton Dashboard SaaS pour lier"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("    ce serveur à ton compte."));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("  → Endpoint cible : %s"), *FactoryMonitor::kWebhookUrl);
    UE_LOG(LogFactoryMonitor, Warning, TEXT("  → Pour révoquer l'accès : supprime la ligne"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("    'ServerAuthToken=' dans Game.ini et redémarre."));
    UE_LOG(LogFactoryMonitor, Warning, TEXT("================================================================"));
    UE_LOG(LogFactoryMonitor, Warning, TEXT(""));
}

// ===========================================================================
// Pipeline principal : Timer → Collecte → Envoi
// ===========================================================================

void UFactoryMonitorSubsystem::OnCollectionTimerFired()
{
    // --- Verrou in-flight ---------------------------------------------------
    // Empêche l'empilement si la requête précédente n'est pas encore terminée
    // (réseau lent, backend surchargé, timeout).
    // Sans ce garde, on accumulerait des requêtes HTTP non résolues en mémoire
    // jusqu'à provoquer un leak ou un spam du backend SaaS.
    if (bRequestInFlight)
    {
        UE_LOG(LogFactoryMonitor, Verbose,
            TEXT("[FactoryMonitor] Tick ignoré — une requête HTTP est déjà en cours."));
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World) || !World->IsServer())
    {
        // Sécurité paranoïaque : ne jamais exécuter si on a perdu l'autorité.
        return;
    }

    UE_LOG(LogFactoryMonitor, Verbose, TEXT("[FactoryMonitor] Début de la collecte de métriques..."));

    // --- Construction du snapshot -------------------------------------------
    TSharedPtr<FJsonObject> Snapshot = BuildFullSnapshot();
    if (!Snapshot.IsValid())
    {
        UE_LOG(LogFactoryMonitor, Warning,
            TEXT("[FactoryMonitor] BuildFullSnapshot() a échoué — tick annulé."));
        return;
    }

    // --- Dispatch de la requête HTTP ----------------------------------------
    // La sérialisation JSON et l'envoi HTTP se font juste après la collecte,
    // toujours sur le GameThread. Le JSON d'un snapshot typique (~50 bâtiments,
    // 3 circuits) se sérialise en < 1 ms — coût négligeable à 1 exécution/min.
    DispatchWebhookRequest(Snapshot.ToSharedRef());
}

// ===========================================================================
// Constructeurs de snapshot par domaine
// ===========================================================================

TSharedPtr<FJsonObject> UFactoryMonitorSubsystem::BuildFullSnapshot() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

    // Métadonnées du snapshot
    Root->SetStringField(TEXT("timestamp"),      FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(TEXT("schema_version"), FactoryMonitor::kSchemaVersion);

    // Agrégation des trois domaines.
    // Chaque collecteur est indépendant : l'échec de l'un n'annule pas les autres.
    if (TSharedPtr<FJsonObject> Inv  = CollectInventoryData())
        Root->SetObjectField(TEXT("inventory"),  Inv);

    if (TSharedPtr<FJsonObject> Prod = CollectProductionData())
        Root->SetObjectField(TEXT("production"), Prod);

    if (TSharedPtr<FJsonObject> Pwr  = CollectPowerData())
        Root->SetObjectField(TEXT("power"),      Pwr);

    return Root;
}

// ---------------------------------------------------------------------------
// Collecteur 1 : Inventaires (AFGBuildableStorage)
//
// OPTIMISATION TPS :
//   AFGBuildableSubsystem::GetBuildables<T>() parcourt uniquement le tableau
//   interne mBuildables (déjà filtré au monde courant), contrairement à
//   TActorIterator qui scanne tout le tableau global des UObject.
//   Sur un serveur avec 100 000+ objets UE, cela représente un facteur
//   d'accélération de 10x à 50x selon la densité de la carte.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UFactoryMonitorSubsystem::CollectInventoryData() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }

    AFGBuildableSubsystem* BuildSub = AFGBuildableSubsystem::Get(World);
    if (!IsValid(BuildSub))
    {
        UE_LOG(LogFactoryMonitor, Warning,
            TEXT("[FactoryMonitor] AFGBuildableSubsystem indisponible — inventaire ignoré."));
        return nullptr;
    }

    // Récupération via le registre natif — O(n_storage) au lieu de O(n_uobjects).
    TArray<AFGBuildableStorage*> Storages;
    BuildSub->GetBuildables<AFGBuildableStorage>(Storages);

    // Pré-allocation pour éviter les réallocations dynamiques dans la boucle.
    TArray<TSharedPtr<FJsonValue>> ContainerArray;
    ContainerArray.Reserve(Storages.Num());

    int32 TotalItems     = 0;
    int32 ContainerCount = 0;

    for (AFGBuildableStorage* Storage : Storages)
    {
        // --- Défenses contre les pointeurs invalides ---
        // Le jeu peut marquer des bâtiments comme PendingKill entre deux ticks.
        if (!IsValid(Storage))
        {
            continue;
        }

        UFGInventoryComponent* Inventory = Storage->GetStorageInventory();
        if (!IsValid(Inventory))
        {
            continue;
        }

        // Somme de tous les stacks de l'inventaire.
        // FInventoryStack::mNumItems (convention de nommage CSS : préfixe "m").
        int32 ItemsHere = 0;
        for (const FInventoryStack& Stack : Inventory->GetInventoryStacks())
        {
            if (Stack.HasItems())
            {
                ItemsHere += Stack.mNumItems;
            }
        }

        TotalItems += ItemsHere;
        ++ContainerCount;

        // Objet JSON par conteneur
        TSharedRef<FJsonObject> ContObj = MakeShared<FJsonObject>();
        const FVector Loc = Storage->GetActorLocation();
        ContObj->SetStringField(TEXT("location"),
            FString::Printf(TEXT("%.0f,%.0f,%.0f"), Loc.X, Loc.Y, Loc.Z));
        ContObj->SetNumberField(TEXT("item_count"), ItemsHere);
        ContainerArray.Add(MakeShared<FJsonValueObject>(ContObj));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("total_items"),     TotalItems);
    Result->SetNumberField(TEXT("container_count"), ContainerCount);
    Result->SetArrayField (TEXT("containers"),      ContainerArray);
    return Result;
}

// ---------------------------------------------------------------------------
// Collecteur 2 : Production (Manufacturers + Extractors)
//
// OPTIMISATION TPS :
//   Même logique que le collecteur d'inventaire : GetBuildables<T>() utilise
//   le registre interne d'AFGBuildableSubsystem.
//   La lambda ProcessFactory évite la duplication de code tout en gardant
//   un seul parcours par type — pas de surcoût lié au polymorphisme virtuel
//   dans la boucle elle-même.
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UFactoryMonitorSubsystem::CollectProductionData() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }

    AFGBuildableSubsystem* BuildSub = AFGBuildableSubsystem::Get(World);
    if (!IsValid(BuildSub))
    {
        UE_LOG(LogFactoryMonitor, Warning,
            TEXT("[FactoryMonitor] AFGBuildableSubsystem indisponible — production ignorée."));
        return nullptr;
    }

    int32  TotalBuildings     = 0;
    int32  ProducingBuildings = 0;
    float  SumEfficiency      = 0.0f;
    TArray<TSharedPtr<FJsonValue>> BuildingArray;

    // Lambda partagée par les deux types de bâtiments producteurs.
    // Capturée par référence — aucune copie de données.
    auto ProcessFactory = [&](AFGBuildableFactory* Factory, const TCHAR* TypeLabel)
    {
        if (!IsValid(Factory))
        {
            return;
        }

        ++TotalBuildings;

        const bool  bProducing = Factory->IsProducing();
        // Potentiel : 1.0 = 100 %, jusqu'à ~2.5 avec overclocking.
        // Clamp défensif : une valeur corrompue ne biaise pas la moyenne.
        const float Potential  = FMath::Clamp(Factory->GetCurrentPotential(), 0.0f, 2.5f);

        if (bProducing)
        {
            ++ProducingBuildings;
            SumEfficiency += Potential;
        }

        TSharedRef<FJsonObject> BObj = MakeShared<FJsonObject>();
        BObj->SetStringField(TEXT("type"),            TypeLabel);
        BObj->SetBoolField  (TEXT("is_producing"),    bProducing);
        // Arrondi à 2 décimales pour alléger la charge JSON côté réseau.
        BObj->SetNumberField(TEXT("efficiency_pct"),
            FMath::RoundToFloat(Potential * 10000.0f) / 100.0f);
        BuildingArray.Add(MakeShared<FJsonValueObject>(BObj));
    };

    // Itération séparée par type — chaque appel GetBuildables<T>() ne filtre
    // que les bâtiments du type demandé dans le registre interne.
    TArray<AFGBuildableManufacturer*> Manufacturers;
    BuildSub->GetBuildables<AFGBuildableManufacturer>(Manufacturers);
    for (AFGBuildableManufacturer* M : Manufacturers)
    {
        ProcessFactory(M, TEXT("Manufacturer"));
    }

    TArray<AFGBuildableResourceExtractor*> Extractors;
    BuildSub->GetBuildables<AFGBuildableResourceExtractor>(Extractors);
    for (AFGBuildableResourceExtractor* E : Extractors)
    {
        ProcessFactory(E, TEXT("Extractor"));
    }

    const float AvgEfficiency = (ProducingBuildings > 0)
        ? (SumEfficiency / static_cast<float>(ProducingBuildings))
        : 0.0f;

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("total_buildings"),     TotalBuildings);
    Result->SetNumberField(TEXT("producing_buildings"), ProducingBuildings);
    Result->SetNumberField(TEXT("avg_efficiency_pct"),
        FMath::RoundToFloat(AvgEfficiency * 10000.0f) / 100.0f);
    Result->SetArrayField (TEXT("buildings"),           BuildingArray);
    return Result;
}

// ---------------------------------------------------------------------------
// Collecteur 3 : Réseau électrique (UFGPowerCircuit)
//
// OPTIMISATION TPS :
//   UFGPowerCircuit est un UObject (non-Actor). TObjectIterator avec filtre
//   de monde est utilisé ici car il parcourt uniquement les objets du type
//   cible dans le GUObjectArray — beaucoup plus petit qu'un TActorIterator
//   sur tous les acteurs.
//   Alternative future : passer par AFGCircuitSubsystem::Get(World) si
//   Coffee Stain expose une API de liste directe dans une version ultérieure
//   du SDK CSS (vérifiez FGCircuitSubsystem.h dans vos headers).
//
// SÉCURITÉ MÉMOIRE :
//   Le filtre GetWorld() != World évite de traiter des circuits appartenant
//   à d'autres mondes (ex. sessions PIE multiples en développement local).
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UFactoryMonitorSubsystem::CollectPowerData() const
{
    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return nullptr;
    }

    float  TotalProduced = 0.0f;
    float  TotalConsumed = 0.0f;
    int32  CircuitIndex  = 0;
    TArray<TSharedPtr<FJsonValue>> CircuitArray;

    for (TObjectIterator<UFGPowerCircuit> It; It; ++It)
    {
        UFGPowerCircuit* Circuit = *It;

        // --- Triple défense ---
        // 1. IsValid : évite les objets marqués PendingKill.
        // 2. Filtre monde : indispensable avec TObjectIterator (global).
        // 3. Cast implicite déjà effectué par le type de l'itérateur.
        if (!IsValid(Circuit))
        {
            continue;
        }
        const UWorld* CircuitWorld = Circuit->GetWorld();
        if (!IsValid(CircuitWorld) || CircuitWorld != World)
        {
            continue;
        }

        // FPowerCircuitStats contient les métriques temps-réel du circuit.
        // Champs CSS : PowerProduced, PowerConsumed, PowerCapacity, PowerMaxConsumed (en MW).
        FPowerCircuitStats Stats;
        Circuit->GetStats(Stats);

        TotalProduced += Stats.PowerProduced;
        TotalConsumed += Stats.PowerConsumed;

        TSharedRef<FJsonObject> CObj = MakeShared<FJsonObject>();
        CObj->SetNumberField(TEXT("circuit_id"),           CircuitIndex++);
        CObj->SetNumberField(TEXT("power_produced_mw"),
            FMath::RoundToFloat(Stats.PowerProduced   * 100.0f) / 100.0f);
        CObj->SetNumberField(TEXT("power_consumed_mw"),
            FMath::RoundToFloat(Stats.PowerConsumed   * 100.0f) / 100.0f);
        CObj->SetNumberField(TEXT("power_capacity_mw"),
            FMath::RoundToFloat(Stats.PowerCapacity   * 100.0f) / 100.0f);
        CObj->SetNumberField(TEXT("power_max_demand_mw"),
            FMath::RoundToFloat(Stats.PowerMaxConsumed * 100.0f) / 100.0f);
        CObj->SetBoolField  (TEXT("is_fused"),  Circuit->IsFuseTriggered());
        CircuitArray.Add(MakeShared<FJsonValueObject>(CObj));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("total_produced_mw"),
        FMath::RoundToFloat(TotalProduced * 100.0f) / 100.0f);
    Result->SetNumberField(TEXT("total_consumed_mw"),
        FMath::RoundToFloat(TotalConsumed * 100.0f) / 100.0f);
    Result->SetNumberField(TEXT("circuit_count"),  CircuitArray.Num());
    Result->SetArrayField (TEXT("circuits"),       CircuitArray);
    return Result;
}

// ===========================================================================
// HTTP — Dispatch et Callback
// ===========================================================================

void UFactoryMonitorSubsystem::DispatchWebhookRequest(TSharedRef<FJsonObject> Payload)
{
    // --- Sérialisation JSON -------------------------------------------------
    // TJsonWriterFactory<TCHAR> produit du JSON UTF-16 (natif UE FString).
    // Pour une charge réseau légèrement réduite, on pourrait utiliser
    // TJsonWriterFactory<char> (UTF-8), mais la différence est négligeable
    // sur les payloads de cette taille.
    FString JsonBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonBody);
    if (!FJsonSerializer::Serialize(Payload, Writer))
    {
        UE_LOG(LogFactoryMonitor, Error,
            TEXT("[FactoryMonitor] Échec de la sérialisation JSON — requête annulée."));
        return;
    }

    // --- Construction de la requête HTTP ------------------------------------
    FHttpModule& Http = FHttpModule::Get();
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http.CreateRequest();

    Request->SetURL(FactoryMonitor::kWebhookUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"),  TEXT("application/json; charset=utf-8"));
    Request->SetHeader(TEXT("User-Agent"),    TEXT("SatisfactoryFactoryMonitor/2.0"));
    // Le token d'auth est injecté dans l'en-tête — jamais dans l'URL ni le body.
    Request->SetHeader(TEXT("Authorization"),
        FString::Printf(TEXT("Bearer %s"), *ServerAuthToken));
    Request->SetContentAsString(JsonBody);
    Request->SetTimeout(FactoryMonitor::kHttpTimeout);

    // --- Callback asynchrone (sécurité mémoire) -----------------------------
    // BindWeakLambda capture un TWeakObjectPtr vers 'this'.
    // Si le sous-système est détruit avant l'arrivée de la réponse (arrêt
    // du serveur pendant une requête en cours), le lambda devient un no-op
    // au lieu de provoquer un accès à un pointeur invalide (Signal 11).
    // IMPORTANT : le TSharedPtr<IHttpRequest> est détenu par le module HTTP
    // pendant toute la durée de la requête, puis libéré automatiquement
    // une fois le callback exécuté — aucune fuite mémoire possible.
    Request->OnProcessRequestComplete().BindWeakLambda(this,
        [this](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            OnHttpRequestComplete(Req, Resp, bSuccess);
        });

    // --- Déclenchement (non-bloquant) ---------------------------------------
    // ProcessRequest() envoie la requête sur le thread HTTP interne d'UE.
    // L'appel retourne immédiatement — le GameThread n'est pas bloqué.
    if (!Request->ProcessRequest())
    {
        UE_LOG(LogFactoryMonitor, Error,
            TEXT("[FactoryMonitor] ProcessRequest() a échoué — module HTTP indisponible."));
        return; // bRequestInFlight reste false, le prochain tick pourra réessayer.
    }

    // Verrou posé APRÈS le succès de ProcessRequest().
    bRequestInFlight = true;

    UE_LOG(LogFactoryMonitor, Verbose,
        TEXT("[FactoryMonitor] Requête envoyée (%d octets) → %s"),
        JsonBody.Len(), *FactoryMonitor::kWebhookUrl);
}

void UFactoryMonitorSubsystem::OnHttpRequestComplete(
    FHttpRequestPtr  Request,
    FHttpResponsePtr Response,
    bool             bWasSuccessful)
{
    // Libération du verrou — TOUJOURS, même en cas d'erreur.
    // C'est la seule écriture sur bRequestInFlight en dehors de DispatchWebhookRequest().
    bRequestInFlight = false;

    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogFactoryMonitor, Warning,
            TEXT("[FactoryMonitor] Échec réseau ou timeout (%.0f s) — prochain envoi dans %.0f s."),
            FactoryMonitor::kHttpTimeout,
            FactoryMonitor::kPollInterval);
        return;
    }

    const int32 StatusCode = Response->GetResponseCode();

    if (StatusCode >= 200 && StatusCode < 300)
    {
        UE_LOG(LogFactoryMonitor, Verbose,
            TEXT("[FactoryMonitor] Payload accepté par le SaaS (HTTP %d)."), StatusCode);
    }
    else if (StatusCode == 401)
    {
        // Le token est invalide côté serveur — afficher un message d'aide.
        UE_LOG(LogFactoryMonitor, Error,
            TEXT("[FactoryMonitor] AUTHENTIFICATION REFUSÉE (HTTP 401)."));
        UE_LOG(LogFactoryMonitor, Error,
            TEXT("[FactoryMonitor] Vérifiez que le token '%s' est bien enregistré sur le Dashboard SaaS."),
            *ServerAuthToken);
    }
    else
    {
        UE_LOG(LogFactoryMonitor, Warning,
            TEXT("[FactoryMonitor] Le SaaS a répondu HTTP %d. Body : %s"),
            StatusCode, *Response->GetContentAsString());
    }

    // Note : le TSharedPtr IHttpRequest (Request) sort de portée ici et est
    // détruit si plus personne d'autre n'en détient une copie.
    // Aucune action manuelle de libération n'est nécessaire.
}
