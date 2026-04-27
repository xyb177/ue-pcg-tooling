#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "PCGData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Data/PCGSurfaceData.h"
#include "PCGProfilerSubsystem.h"

namespace PCGProfilerPhase
{
    static FString ResolvePhase(const double PrepareDataMs, const double ExecutionMs, const double PostExecuteMs)
    {
        const double SafePrepare = FMath::Max(0.0, PrepareDataMs);
        const double SafeExecute = FMath::Max(0.0, ExecutionMs);
        const double SafePost = FMath::Max(0.0, PostExecuteMs);

        if (SafePrepare >= SafeExecute && SafePrepare >= SafePost && SafePrepare > KINDA_SMALL_NUMBER)
        {
            return TEXT("PrepareData");
        }

        if (SafePost >= SafeExecute && SafePost >= SafePrepare && SafePost > KINDA_SMALL_NUMBER)
        {
            return TEXT("PostExecute");
        }

        return TEXT("Execute");
    }
}

namespace PCGProfilerScaleStats
{
    static constexpr int64 EstimatedBytesPerPoint = 160;

    static int64 EstimateMemoryBytes(const int64 InputPointCount, const int64 OutputPointCount)
    {
        const int64 SafeInputPoints = FMath::Max<int64>(0, InputPointCount);
        const int64 SafeOutputPoints = FMath::Max<int64>(0, OutputPointCount);
        return (SafeInputPoints + SafeOutputPoints) * EstimatedBytesPerPoint;
    }

    static double SafeRatio(const double Numerator, const double Denominator, const double DefaultValue = 0.0)
    {
        return Denominator > 0.0 ? (Numerator / Denominator) : DefaultValue;
    }
}

namespace PCGProfilerDataTyping
{
    static FString InferDataTypeFromCollection(const FPCGDataCollection& Collection)
    {
        bool bHasPoint = false;
        bool bHasSurface = false;
        bool bHasSpatial = false;

        for (const FPCGTaggedData& TaggedData : Collection.TaggedData)
        {
            const UPCGData* Data = TaggedData.Data.Get();
            if (!Data)
            {
                continue;
            }

            if (Cast<const UPCGBasePointData>(Data))
            {
                bHasPoint = true;
                continue;
            }

            if (Cast<const UPCGSurfaceData>(Data))
            {
                bHasSurface = true;
                continue;
            }

            if (Cast<const UPCGSpatialData>(Data))
            {
                bHasSpatial = true;
            }
        }

        if (bHasPoint)
        {
            return TEXT("Point");
        }
        if (bHasSurface)
        {
            return TEXT("Surface");
        }
        if (bHasSpatial)
        {
            return TEXT("Spatial");
        }
        return TEXT("Unknown");
    }
}

namespace PCGProfilerSerialization
{
    static double GetNumberFieldOr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Name, double DefaultValue = 0.0)
    {
        double Value = DefaultValue;
        if (Obj.IsValid() && Obj->TryGetNumberField(Name, Value))
        {
            return Value;
        }
        FString StringValue;
        if (Obj.IsValid() && Obj->TryGetStringField(Name, StringValue))
        {
            if (StringValue.IsNumeric())
            {
                return FCString::Atod(*StringValue);
            }
        }
        return DefaultValue;
    }

    static int64 GetInt64FieldOr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Name, int64 DefaultValue = 0)
    {
        const double Raw = GetNumberFieldOr(Obj, Name, static_cast<double>(DefaultValue));
        if (Raw >= static_cast<double>(MAX_int64))
        {
            return MAX_int64;
        }
        if (Raw <= static_cast<double>(MIN_int64))
        {
            return MIN_int64;
        }
        return static_cast<int64>(Raw);
    }

    static FString GetStringFieldOr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Name, const FString& DefaultValue = FString())
    {
        FString Value;
        if (Obj.IsValid() && Obj->TryGetStringField(Name, Value))
        {
            return Value;
        }
        return DefaultValue;
    }

    static bool GetBoolFieldOr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Name, bool bDefaultValue = false)
    {
        bool bValue = bDefaultValue;
        if (Obj.IsValid() && Obj->TryGetBoolField(Name, bValue))
        {
            return bValue;
        }
        return bDefaultValue;
    }

    static bool ParseEventJsonLine(const FString& JsonLine, FPCGProfilerNodeEvent& OutEvent)
    {
        TSharedPtr<FJsonObject> Obj;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
        if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
        {
            return false;
        }

        OutEvent.NodeId = GetStringFieldOr(Obj, TEXT("node_id"));
        OutEvent.NodeTitle = GetStringFieldOr(Obj, TEXT("node_title"));
        OutEvent.NodeClass = GetStringFieldOr(Obj, TEXT("node_class"));
        OutEvent.SettingsClass = GetStringFieldOr(Obj, TEXT("settings_class"));
        OutEvent.ExecutionPath = GetStringFieldOr(Obj, TEXT("execution_path"));
        OutEvent.ParentExecutionPath = GetStringFieldOr(Obj, TEXT("parent_execution_path"));
        OutEvent.Phase = GetStringFieldOr(Obj, TEXT("phase"));
        OutEvent.GraphName = GetStringFieldOr(Obj, TEXT("graph_name"));
        OutEvent.ComponentName = GetStringFieldOr(Obj, TEXT("component_name"));
        OutEvent.DurationMs = GetNumberFieldOr(Obj, TEXT("duration_ms"));
        OutEvent.SelfMs = GetNumberFieldOr(Obj, TEXT("self_ms"));
        OutEvent.InclusiveMs = GetNumberFieldOr(Obj, TEXT("inclusive_ms"));
        OutEvent.FirstSeenTimeMs = GetNumberFieldOr(Obj, TEXT("first_seen_time_ms"));
        OutEvent.FirstSeenTimeSource = GetStringFieldOr(Obj, TEXT("first_seen_time_source"), TEXT("chunk_replay"));
        OutEvent.InputCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("input_count")));
        OutEvent.OutputCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("output_count")));
        OutEvent.InputPoints = GetInt64FieldOr(Obj, TEXT("input_points"));
        OutEvent.OutputPoints = GetInt64FieldOr(Obj, TEXT("output_points"));
        OutEvent.WarningCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("warning_count")));
        OutEvent.ErrorCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("error_count")));
        OutEvent.bCancelled = GetBoolFieldOr(Obj, TEXT("cancelled"));
        OutEvent.ThreadGroup = GetStringFieldOr(Obj, TEXT("thread_group"), TEXT("Unknown"));
        OutEvent.ThreadSource = GetStringFieldOr(Obj, TEXT("thread_source"), TEXT("chunk_replay"));
        OutEvent.QueueWaitMs = GetNumberFieldOr(Obj, TEXT("queue_wait_ms"));
        OutEvent.ExecutionMs = GetNumberFieldOr(Obj, TEXT("execution_ms"));
        OutEvent.PrepareDataMs = GetNumberFieldOr(Obj, TEXT("prepare_data_ms"));
        OutEvent.PostExecuteMs = GetNumberFieldOr(Obj, TEXT("post_execute_ms"));
        OutEvent.bCacheHit = GetBoolFieldOr(Obj, TEXT("cache_hit"));
        OutEvent.CacheMissReason = GetStringFieldOr(Obj, TEXT("cache_miss_reason"));
        OutEvent.MemoryBeforeBytes = GetInt64FieldOr(Obj, TEXT("memory_before_bytes"));
        OutEvent.MemoryAfterBytes = GetInt64FieldOr(Obj, TEXT("memory_after_bytes"));
        OutEvent.MemoryDeltaBytes = GetInt64FieldOr(Obj, TEXT("memory_delta_bytes"));
        OutEvent.MemoryMeasurementMode = GetStringFieldOr(Obj, TEXT("memory_measurement_mode"), TEXT("chunk_replay"));
        OutEvent.MemoryScope = GetStringFieldOr(Obj, TEXT("memory_scope"), TEXT("process_estimate"));
        OutEvent.RunMode = GetStringFieldOr(Obj, TEXT("run_mode"), TEXT("editor"));
        OutEvent.WorldType = GetStringFieldOr(Obj, TEXT("world_type"), TEXT("Editor"));
        OutEvent.bIsPIE = GetBoolFieldOr(Obj, TEXT("is_pie"), false);
        OutEvent.bIsCooked = GetBoolFieldOr(Obj, TEXT("is_cooked"), false);
        OutEvent.CellId = GetStringFieldOr(Obj, TEXT("cell_id"));
        OutEvent.StreamingEvent = GetStringFieldOr(Obj, TEXT("streaming_event"));
        OutEvent.GenerateReason = GetStringFieldOr(Obj, TEXT("generate_reason"), TEXT("unknown"));
        OutEvent.SpawnCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("spawn_count")));
        OutEvent.DestroyCount = static_cast<int32>(GetNumberFieldOr(Obj, TEXT("destroy_count")));
        OutEvent.SpawnMs = GetNumberFieldOr(Obj, TEXT("spawn_ms"));
        OutEvent.DestroyMs = GetNumberFieldOr(Obj, TEXT("destroy_ms"));

        int32 MissingRequired = 0;
        MissingRequired += Obj->HasField(TEXT("run_mode")) ? 0 : 1;
        MissingRequired += Obj->HasField(TEXT("world_type")) ? 0 : 1;
        MissingRequired += Obj->HasField(TEXT("input_points")) ? 0 : 1;
        MissingRequired += Obj->HasField(TEXT("output_points")) ? 0 : 1;
        if (MissingRequired > 0)
        {
            OutEvent.ThreadSource = FString::Printf(TEXT("%s|missing_required=%d"), *OutEvent.ThreadSource, MissingRequired);
        }
        return true;
    }

    static TSharedPtr<FJsonObject> BuildEventJsonObject(const FPCGProfilerNodeEvent& Event)
    {
        TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
        EventObj->SetStringField(TEXT("node_id"), Event.NodeId);
        EventObj->SetStringField(TEXT("node_title"), Event.NodeTitle);
        EventObj->SetStringField(TEXT("node_name"), Event.NodeTitle);
        EventObj->SetStringField(TEXT("node_class"), Event.NodeClass);
        EventObj->SetStringField(TEXT("settings_class"), Event.SettingsClass);
        EventObj->SetStringField(TEXT("execution_path"), Event.ExecutionPath);
        EventObj->SetStringField(TEXT("parent_execution_path"), Event.ParentExecutionPath);
        EventObj->SetStringField(TEXT("phase"), Event.Phase);
        EventObj->SetStringField(TEXT("graph_name"), Event.GraphName);
        EventObj->SetStringField(TEXT("component_name"), Event.ComponentName);
        EventObj->SetNumberField(TEXT("duration_ms"), Event.DurationMs);
        EventObj->SetNumberField(TEXT("self_ms"), Event.SelfMs);
        EventObj->SetNumberField(TEXT("inclusive_ms"), Event.InclusiveMs);
        EventObj->SetNumberField(TEXT("first_seen_time_ms"), Event.FirstSeenTimeMs);
        EventObj->SetStringField(TEXT("first_seen_time_source"), Event.FirstSeenTimeSource);
        EventObj->SetNumberField(TEXT("input_count"), Event.InputCount);
        EventObj->SetNumberField(TEXT("output_count"), Event.OutputCount);
        EventObj->SetNumberField(TEXT("input_points"), static_cast<double>(Event.InputPoints));
        EventObj->SetStringField(TEXT("input_points_i64"), LexToString(Event.InputPoints));
        EventObj->SetNumberField(TEXT("output_points"), static_cast<double>(Event.OutputPoints));
        EventObj->SetStringField(TEXT("output_points_i64"), LexToString(Event.OutputPoints));
        EventObj->SetNumberField(TEXT("estimated_memory_bytes"), static_cast<double>(PCGProfilerScaleStats::EstimateMemoryBytes(Event.InputPoints, Event.OutputPoints)));
        EventObj->SetNumberField(TEXT("warning_count"), Event.WarningCount);
        EventObj->SetNumberField(TEXT("error_count"), Event.ErrorCount);
        EventObj->SetBoolField(TEXT("cancelled"), Event.bCancelled);
        EventObj->SetStringField(TEXT("thread_group"), Event.ThreadGroup);
        EventObj->SetStringField(TEXT("thread_source"), Event.ThreadSource);
        EventObj->SetNumberField(TEXT("queue_wait_ms"), Event.QueueWaitMs);
        EventObj->SetNumberField(TEXT("execution_ms"), Event.ExecutionMs);
        EventObj->SetNumberField(TEXT("prepare_data_ms"), Event.PrepareDataMs);
        EventObj->SetNumberField(TEXT("post_execute_ms"), Event.PostExecuteMs);
        EventObj->SetBoolField(TEXT("cache_hit"), Event.bCacheHit);
        EventObj->SetStringField(TEXT("cache_miss_reason"), Event.CacheMissReason);
        EventObj->SetNumberField(TEXT("memory_before_bytes"), static_cast<double>(Event.MemoryBeforeBytes));
        EventObj->SetStringField(TEXT("memory_before_bytes_i64"), LexToString(Event.MemoryBeforeBytes));
        EventObj->SetNumberField(TEXT("memory_after_bytes"), static_cast<double>(Event.MemoryAfterBytes));
        EventObj->SetStringField(TEXT("memory_after_bytes_i64"), LexToString(Event.MemoryAfterBytes));
        EventObj->SetNumberField(TEXT("memory_delta_bytes"), static_cast<double>(Event.MemoryDeltaBytes));
        EventObj->SetStringField(TEXT("memory_delta_bytes_i64"), LexToString(Event.MemoryDeltaBytes));
        EventObj->SetStringField(TEXT("memory_measurement_mode"), Event.MemoryMeasurementMode);
        EventObj->SetStringField(TEXT("memory_scope"), Event.MemoryScope);
        EventObj->SetStringField(TEXT("data_type"), Event.DataType);
        EventObj->SetStringField(TEXT("run_mode"), Event.RunMode);
        EventObj->SetStringField(TEXT("world_type"), Event.WorldType);
        EventObj->SetBoolField(TEXT("is_pie"), Event.bIsPIE);
        EventObj->SetBoolField(TEXT("is_cooked"), Event.bIsCooked);
        EventObj->SetStringField(TEXT("cell_id"), Event.CellId);
        EventObj->SetStringField(TEXT("streaming_event"), Event.StreamingEvent);
        EventObj->SetStringField(TEXT("generate_reason"), Event.GenerateReason);
        EventObj->SetNumberField(TEXT("spawn_count"), Event.SpawnCount);
        EventObj->SetNumberField(TEXT("destroy_count"), Event.DestroyCount);
        EventObj->SetNumberField(TEXT("spawn_ms"), Event.SpawnMs);
        EventObj->SetNumberField(TEXT("destroy_ms"), Event.DestroyMs);
        return EventObj;
    }
}
