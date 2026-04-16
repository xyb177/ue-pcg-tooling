#include "PCGProfilerSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace PCGProfilerAutomation
{
	static bool FindAggregateByName(const TArray<FPCGProfilerNodeAggregate>& Aggregates, const FString& NodeName, FPCGProfilerNodeAggregate& OutAggregate)
	{
		for (const FPCGProfilerNodeAggregate& Aggregate : Aggregates)
		{
			if (Aggregate.NodeName == NodeName)
			{
				OutAggregate = Aggregate;
				return true;
			}
		}

		return false;
	}

	static bool FindObjectByStringField(const TArray<TSharedPtr<FJsonValue>>& Array, const FString& FieldName, const FString& FieldValue, TSharedPtr<FJsonObject>& OutObject)
	{
		for (const TSharedPtr<FJsonValue>& Item : Array)
		{
			if (!Item.IsValid() || Item->Type != EJson::Object)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> Obj = Item->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			FString Value;
			if (Obj->TryGetStringField(FieldName, Value) && Value == FieldValue)
			{
				OutObject = Obj;
				return true;
			}
		}

		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGProfilerAggregateStatsTest, "Plugins.PCGProfiler.Aggregates.BasicStats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGProfilerAggregateStatsTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("GEngine is null."));
		return false;
	}

	UPCGProfilerSubsystem* Subsystem = GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>();
	if (!Subsystem)
	{
		AddError(TEXT("UPCGProfilerSubsystem is unavailable."));
		return false;
	}

	Subsystem->SetStrictCriticalPathMode(false);
	Subsystem->StartRun(TEXT("Automation_Aggregates"));

	FPCGProfilerNodeEvent A1;
	A1.NodeId = TEXT("NodeA");
	A1.NodeTitle = TEXT("NodeA");
	A1.GraphName = TEXT("GraphA");
	A1.Phase = TEXT("Execute");
	A1.InclusiveMs = 4.0;
	A1.SelfMs = 4.0;
	A1.DurationMs = 4.0;
	Subsystem->RecordNodeEvent(A1);

	FPCGProfilerNodeEvent A2 = A1;
	A2.InclusiveMs = 10.0;
	A2.SelfMs = 10.0;
	A2.DurationMs = 10.0;
	Subsystem->RecordNodeEvent(A2);

	FPCGProfilerNodeEvent B1;
	B1.NodeId = TEXT("NodeB");
	B1.NodeTitle = TEXT("NodeB");
	B1.GraphName = TEXT("GraphA");
	B1.Phase = TEXT("Execute");
	B1.InclusiveMs = 3.0;
	B1.SelfMs = 3.0;
	B1.DurationMs = 3.0;
	Subsystem->RecordNodeEvent(B1);

	const TArray<FPCGProfilerNodeAggregate> Aggregates = Subsystem->GetNodeAggregates();
	TestTrue(TEXT("Aggregates should not be empty"), Aggregates.Num() >= 2);

	FPCGProfilerNodeAggregate NodeA;
	const bool bFoundNodeA = PCGProfilerAutomation::FindAggregateByName(Aggregates, TEXT("NodeA"), NodeA);
	TestTrue(TEXT("NodeA aggregate should exist"), bFoundNodeA);
	if (bFoundNodeA)
	{
		TestEqual(TEXT("NodeA call count"), NodeA.CallCount, 2);
		TestEqual(TEXT("NodeA total ms"), NodeA.TotalMs, 14.0);
		TestEqual(TEXT("NodeA max ms"), NodeA.MaxMs, 10.0);
		TestEqual(TEXT("NodeA min ms"), NodeA.MinMs, 4.0);
		TestEqual(TEXT("NodeA avg ms"), NodeA.AvgMs, 7.0);
		TestEqual(TEXT("NodeA graph name"), NodeA.GraphName, FString(TEXT("GraphA")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGProfilerFullFeatureCoverageTest, "Plugins.PCGProfiler.Export.FullFeatureCoverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGProfilerFullFeatureCoverageTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("GEngine is null."));
		return false;
	}

	UPCGProfilerSubsystem* Subsystem = GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>();
	if (!Subsystem)
	{
		AddError(TEXT("UPCGProfilerSubsystem is unavailable."));
		return false;
	}

	Subsystem->SetStrictCriticalPathMode(true);
	Subsystem->StartRun(TEXT("Automation_CrossRun_1"));

	FPCGProfilerNodeEvent Run1X1;
	Run1X1.NodeId = TEXT("NodeX-GUID");
	Run1X1.NodeTitle = TEXT("NodeX");
	Run1X1.NodeClass = TEXT("ClassX");
	Run1X1.GraphName = TEXT("GraphFull");
	Run1X1.Phase = TEXT("Execute");
	Run1X1.ThreadGroup = TEXT("GameThread");
	Run1X1.InclusiveMs = 2.0;
	Run1X1.SelfMs = 1.0;
	Run1X1.DurationMs = 2.0;
	Subsystem->RecordNodeEvent(Run1X1);

	FPCGProfilerNodeEvent Run1X2 = Run1X1;
	Run1X2.InclusiveMs = 10.0;
	Run1X2.SelfMs = 9.0;
	Run1X2.DurationMs = 10.0;
	Subsystem->RecordNodeEvent(Run1X2);

	Subsystem->StartRun(TEXT("Automation_CrossRun_2"));

	FPCGProfilerNodeEvent Parent;
	Parent.NodeId = TEXT("Parent-GUID");
	Parent.NodeTitle = TEXT("Parent");
	Parent.NodeClass = TEXT("ParentType");
	Parent.GraphName = TEXT("GraphFull");
	Parent.ExecutionPath = TEXT("/Root/Parent");
	Parent.Phase = TEXT("PrepareData");
	Parent.ThreadGroup = TEXT("GameThread");
	Parent.InclusiveMs = 20.0;
	Parent.SelfMs = 20.0;
	Parent.DurationMs = 20.0;
	Parent.ExecutionMs = 20.0;
	Parent.FirstSeenTimeMs = 0.0;
	Subsystem->RecordNodeEvent(Parent);

	FPCGProfilerNodeEvent Child;
	Child.NodeId = TEXT("Child-GUID");
	Child.NodeTitle = TEXT("Child");
	Child.NodeClass = TEXT("ChildType");
	Child.GraphName = TEXT("GraphFull");
	Child.ExecutionPath = TEXT("/Root/Parent/Child");
	Child.ParentExecutionPath = TEXT("/Root/Parent");
	Child.Phase = TEXT("Execute");
	Child.ThreadGroup = TEXT("GameThread");
	Child.InclusiveMs = 7.0;
	Child.SelfMs = 7.0;
	Child.DurationMs = 7.0;
	Child.ExecutionMs = 7.0;
	Child.FirstSeenTimeMs = 2.0;
	Subsystem->RecordNodeEvent(Child);

	FPCGProfilerNodeEvent NodeXRun2;
	NodeXRun2.NodeId = TEXT("NodeX-GUID");
	NodeXRun2.NodeTitle = TEXT("NodeX");
	NodeXRun2.NodeClass = TEXT("ClassX");
	NodeXRun2.GraphName = TEXT("GraphFull");
	NodeXRun2.Phase = TEXT("PostExecute");
	NodeXRun2.ThreadGroup = TEXT("GameThread");
	NodeXRun2.InclusiveMs = 6.0;
	NodeXRun2.SelfMs = 5.0;
	NodeXRun2.DurationMs = 6.0;
	NodeXRun2.ExecutionMs = 6.0;
	NodeXRun2.FirstSeenTimeMs = 22.0;
	Subsystem->RecordNodeEvent(NodeXRun2);

	FPCGProfilerNodeEvent WorkerNode;
	WorkerNode.NodeId = TEXT("Worker-GUID");
	WorkerNode.NodeTitle = TEXT("WorkerNode");
	WorkerNode.NodeClass = TEXT("WorkerType");
	WorkerNode.GraphName = TEXT("GraphFull");
	WorkerNode.Phase = TEXT("Execute");
	WorkerNode.InclusiveMs = 3.0;
	WorkerNode.SelfMs = 3.0;
	WorkerNode.DurationMs = 3.0;
	WorkerNode.ExecutionMs = 3.0;
	WorkerNode.QueueWaitMs = 1.5;
	WorkerNode.FirstSeenTimeMs = 5.0;
	WorkerNode.ThreadGroup = TEXT("Worker");
	Subsystem->RecordNodeEvent(WorkerNode);

	FPCGProfilerNodeEvent WorkerNode2 = WorkerNode;
	WorkerNode2.NodeId = TEXT("Worker2-GUID");
	WorkerNode2.NodeTitle = TEXT("WorkerNode2");
	WorkerNode2.InclusiveMs = 4.0;
	WorkerNode2.SelfMs = 4.0;
	WorkerNode2.DurationMs = 4.0;
	WorkerNode2.ExecutionMs = 4.0;
	WorkerNode2.QueueWaitMs = 0.5;
	WorkerNode2.FirstSeenTimeMs = 6.0;
	Subsystem->RecordNodeEvent(WorkerNode2);

	FString SavedPath;
	const bool bExported = Subsystem->ExportJsonReport(TEXT("Tests/PCGProfiler/AutomationFullCoverage.json"), SavedPath);
	TestTrue(TEXT("ExportJsonReport should succeed"), bExported);
	if (!bExported)
	{
		return false;
	}

	FString JsonText;
	const bool bLoaded = FFileHelper::LoadFileToString(JsonText, *SavedPath);
	TestTrue(TEXT("Exported JSON file should be readable"), bLoaded);
	if (!bLoaded)
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Exported JSON should parse"), bParsed);
	if (!bParsed)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* TopNObj = nullptr;
	TestTrue(TEXT("top_n object exists"), RootObject->TryGetObjectField(TEXT("top_n"), TopNObj) && TopNObj && TopNObj->IsValid());
	if (TopNObj && TopNObj->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ByTotal = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ByPeak = nullptr;
		TestTrue(TEXT("top_n.by_total_ms exists"), (*TopNObj)->TryGetArrayField(TEXT("by_total_ms"), ByTotal));
		TestTrue(TEXT("top_n.by_peak_ms exists"), (*TopNObj)->TryGetArrayField(TEXT("by_peak_ms"), ByPeak));
		if (ByTotal && ByTotal->Num() > 0)
		{
			const TSharedPtr<FJsonObject> FirstTotal = (*ByTotal)[0]->AsObject();
			FString NodeName;
			FirstTotal->TryGetStringField(TEXT("node_name"), NodeName);
			TestEqual(TEXT("Top total node should be Parent"), NodeName, FString(TEXT("Parent")));
		}
		if (ByPeak && ByPeak->Num() > 0)
		{
			const TSharedPtr<FJsonObject> FirstPeak = (*ByPeak)[0]->AsObject();
			FString NodeName;
			FirstPeak->TryGetStringField(TEXT("node_name"), NodeName);
			TestEqual(TEXT("Top peak node should be Parent"), NodeName, FString(TEXT("Parent")));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	TestTrue(TEXT("nodes array exists"), RootObject->TryGetArrayField(TEXT("nodes"), Nodes));
	if (Nodes)
	{
		TSharedPtr<FJsonObject> ParentNodeObj;
		const bool bFoundParentNode = PCGProfilerAutomation::FindObjectByStringField(*Nodes, TEXT("node_id"), TEXT("Parent-GUID"), ParentNodeObj);
		TestTrue(TEXT("Parent node object exists"), bFoundParentNode);
		if (bFoundParentNode && ParentNodeObj.IsValid())
		{
			double SelfMs = 0.0;
			double InclusiveMs = 0.0;
			ParentNodeObj->TryGetNumberField(TEXT("self_ms"), SelfMs);
			ParentNodeObj->TryGetNumberField(TEXT("inclusive_ms"), InclusiveMs);
			TestTrue(TEXT("Parent inclusive should be 20"), FMath::IsNearlyEqual(InclusiveMs, 20.0, 0.001));
			TestTrue(TEXT("Parent self should be 13 (20 - child 7)"), FMath::IsNearlyEqual(SelfMs, 13.0, 0.001));
		}

		TSharedPtr<FJsonObject> WorkerNodeObj;
		const bool bFoundWorkerNode = PCGProfilerAutomation::FindObjectByStringField(*Nodes, TEXT("node_id"), TEXT("Worker-GUID"), WorkerNodeObj);
		TestTrue(TEXT("Worker node object exists"), bFoundWorkerNode);
		if (bFoundWorkerNode && WorkerNodeObj.IsValid())
		{
			double WorkerMs = 0.0;
			double WorkerCalls = 0.0;
			double GameThreadMs = 0.0;
			WorkerNodeObj->TryGetNumberField(TEXT("worker_thread_ms"), WorkerMs);
			WorkerNodeObj->TryGetNumberField(TEXT("worker_thread_call_count"), WorkerCalls);
			WorkerNodeObj->TryGetNumberField(TEXT("game_thread_ms"), GameThreadMs);
			TestTrue(TEXT("Worker node worker_thread_ms should be 3"), FMath::IsNearlyEqual(WorkerMs, 3.0, 0.001));
			TestTrue(TEXT("Worker node worker_thread_call_count should be 1"), FMath::IsNearlyEqual(WorkerCalls, 1.0, 0.001));
			TestTrue(TEXT("Worker node game_thread_ms should be 0"), FMath::IsNearlyEqual(GameThreadMs, 0.0, 0.001));
		}
	}

	const TSharedPtr<FJsonObject>* ThreadLoad = nullptr;
	TestTrue(TEXT("thread_load object exists"), RootObject->TryGetObjectField(TEXT("thread_load"), ThreadLoad) && ThreadLoad && ThreadLoad->IsValid());
	if (ThreadLoad && ThreadLoad->IsValid())
	{
		double GameThreadTotalMs = 0.0;
		double WorkerTotalMs = 0.0;
		double WorkerCallCount = 0.0;
		(*ThreadLoad)->TryGetNumberField(TEXT("game_thread_total_ms"), GameThreadTotalMs);
		(*ThreadLoad)->TryGetNumberField(TEXT("worker_total_ms"), WorkerTotalMs);
		(*ThreadLoad)->TryGetNumberField(TEXT("worker_call_count"), WorkerCallCount);
		TestTrue(TEXT("game_thread_total_ms should be 33"), FMath::IsNearlyEqual(GameThreadTotalMs, 33.0, 0.001));
		TestTrue(TEXT("worker_total_ms should be 7"), FMath::IsNearlyEqual(WorkerTotalMs, 7.0, 0.001));
		TestTrue(TEXT("worker_call_count should be 2"), FMath::IsNearlyEqual(WorkerCallCount, 2.0, 0.001));

		const TArray<TSharedPtr<FJsonValue>>* ByPhase = nullptr;
		TestTrue(TEXT("thread_load.by_phase exists"), (*ThreadLoad)->TryGetArrayField(TEXT("by_phase"), ByPhase));
		TestTrue(TEXT("thread_load.by_phase non-empty"), ByPhase && ByPhase->Num() > 0);

		const TArray<TSharedPtr<FJsonValue>>* TimelineWindows = nullptr;
		TestTrue(TEXT("thread_load.timeline_windows exists"), (*ThreadLoad)->TryGetArrayField(TEXT("timeline_windows"), TimelineWindows));
		TestTrue(TEXT("thread_load.timeline_windows non-empty"), TimelineWindows && TimelineWindows->Num() > 0);

		const TSharedPtr<FJsonObject>* WorkerParallelism = nullptr;
		TestTrue(TEXT("thread_load.worker_parallelism exists"), (*ThreadLoad)->TryGetObjectField(TEXT("worker_parallelism"), WorkerParallelism) && WorkerParallelism && WorkerParallelism->IsValid());
		if (WorkerParallelism && WorkerParallelism->IsValid())
		{
			double EffectiveConcurrency = 0.0;
			(*WorkerParallelism)->TryGetNumberField(TEXT("effective_concurrency"), EffectiveConcurrency);
			TestTrue(TEXT("worker effective concurrency should be > 0"), EffectiveConcurrency > 0.0);
		}

		const TSharedPtr<FJsonObject>* QueueWait = nullptr;
		TestTrue(TEXT("thread_load.queue_wait_vs_execution exists"), (*ThreadLoad)->TryGetObjectField(TEXT("queue_wait_vs_execution"), QueueWait) && QueueWait && QueueWait->IsValid());

		const TArray<TSharedPtr<FJsonValue>>* Jitter = nullptr;
		TestTrue(TEXT("thread_load.thread_jitter exists"), (*ThreadLoad)->TryGetArrayField(TEXT("thread_jitter"), Jitter));

		const TSharedPtr<FJsonObject>* Scheduling = nullptr;
		TestTrue(TEXT("thread_load.scheduling_bottlenecks exists"), (*ThreadLoad)->TryGetObjectField(TEXT("scheduling_bottlenecks"), Scheduling) && Scheduling && Scheduling->IsValid());

		const TSharedPtr<FJsonObject>* ParallelEfficiency = nullptr;
		TestTrue(TEXT("thread_load.parallel_efficiency exists"), (*ThreadLoad)->TryGetObjectField(TEXT("parallel_efficiency"), ParallelEfficiency) && ParallelEfficiency && ParallelEfficiency->IsValid());
		if (ParallelEfficiency && ParallelEfficiency->IsValid())
		{
			double Score = 0.0;
			(*ParallelEfficiency)->TryGetNumberField(TEXT("score"), Score);
			TestTrue(TEXT("parallel efficiency score in [0,100]"), Score >= 0.0 && Score <= 100.0);
		}

		const TSharedPtr<FJsonObject>* Concurrency = nullptr;
		TestTrue(TEXT("thread_load.concurrency exists"), (*ThreadLoad)->TryGetObjectField(TEXT("concurrency"), Concurrency) && Concurrency && Concurrency->IsValid());
		if (Concurrency && Concurrency->IsValid())
		{
			double PeakParallel = 0.0;
			(*Concurrency)->TryGetNumberField(TEXT("peak_parallel_events"), PeakParallel);
			TestTrue(TEXT("peak_parallel_events should be >= 1"), PeakParallel >= 1.0);
		}

		const TSharedPtr<FJsonObject>* Imbalance = nullptr;
		TestTrue(TEXT("thread_load.imbalance exists"), (*ThreadLoad)->TryGetObjectField(TEXT("imbalance"), Imbalance) && Imbalance && Imbalance->IsValid());

		const TSharedPtr<FJsonObject>* TopNodesByThread = nullptr;
		TestTrue(TEXT("thread_load.top_nodes_by_thread exists"), (*ThreadLoad)->TryGetObjectField(TEXT("top_nodes_by_thread"), TopNodesByThread) && TopNodesByThread && TopNodesByThread->IsValid());
		if (TopNodesByThread && TopNodesByThread->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* TopWorkerNodes = nullptr;
			TestTrue(TEXT("top_worker_nodes exists"), (*TopNodesByThread)->TryGetArrayField(TEXT("top_worker_nodes"), TopWorkerNodes));
			TestTrue(TEXT("top_worker_nodes non-empty"), TopWorkerNodes && TopWorkerNodes->Num() > 0);
		}

		const TSharedPtr<FJsonObject>* PerRunSummary = nullptr;
		TestTrue(TEXT("thread_load.per_run_summary exists"), (*ThreadLoad)->TryGetObjectField(TEXT("per_run_summary"), PerRunSummary) && PerRunSummary && PerRunSummary->IsValid());
		if (PerRunSummary && PerRunSummary->IsValid())
		{
			double RunCount = 0.0;
			(*PerRunSummary)->TryGetNumberField(TEXT("run_count"), RunCount);
			TestTrue(TEXT("per_run_summary.run_count should be >= 2"), RunCount >= 2.0);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* PhaseBuckets = nullptr;
	TestTrue(TEXT("phase_buckets array exists"), RootObject->TryGetArrayField(TEXT("phase_buckets"), PhaseBuckets));
	if (PhaseBuckets)
	{
		TSharedPtr<FJsonObject> Prepare;
		TSharedPtr<FJsonObject> Execute;
		TSharedPtr<FJsonObject> Post;
		TestTrue(TEXT("PrepareData bucket exists"), PCGProfilerAutomation::FindObjectByStringField(*PhaseBuckets, TEXT("phase"), TEXT("PrepareData"), Prepare));
		TestTrue(TEXT("Execute bucket exists"), PCGProfilerAutomation::FindObjectByStringField(*PhaseBuckets, TEXT("phase"), TEXT("Execute"), Execute));
		TestTrue(TEXT("PostExecute bucket exists"), PCGProfilerAutomation::FindObjectByStringField(*PhaseBuckets, TEXT("phase"), TEXT("PostExecute"), Post));
	}

	const TArray<TSharedPtr<FJsonValue>>* CrossRun = nullptr;
	TestTrue(TEXT("cross_run_node_stats array exists"), RootObject->TryGetArrayField(TEXT("cross_run_node_stats"), CrossRun));
	if (CrossRun)
	{
		TSharedPtr<FJsonObject> NodeXCrossObj;
		const bool bFoundNodeXCross = PCGProfilerAutomation::FindObjectByStringField(*CrossRun, TEXT("node_key"), TEXT("GraphFull|NodeX-GUID|NodeX"), NodeXCrossObj);
		TestTrue(TEXT("NodeX cross-run entry exists"), bFoundNodeXCross);
		if (bFoundNodeXCross && NodeXCrossObj.IsValid())
		{
			double InclusiveP50 = 0.0;
			double InclusiveP95 = 0.0;
			double TotalCalls = 0.0;
			NodeXCrossObj->TryGetNumberField(TEXT("inclusive_p50_ms"), InclusiveP50);
			NodeXCrossObj->TryGetNumberField(TEXT("inclusive_p95_ms"), InclusiveP95);
			NodeXCrossObj->TryGetNumberField(TEXT("total_calls"), TotalCalls);
			TestTrue(TEXT("NodeX total calls should be 3"), FMath::IsNearlyEqual(TotalCalls, 3.0, 0.001));
			TestTrue(TEXT("NodeX inclusive p50 should be 6"), FMath::IsNearlyEqual(InclusiveP50, 6.0, 0.001));
            TestTrue(TEXT("NodeX inclusive p95 should be 9.6 (linear interpolation)"), FMath::IsNearlyEqual(InclusiveP95, 9.6, 0.001));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* CriticalPath = nullptr;
	TestTrue(TEXT("critical_path array exists"), RootObject->TryGetArrayField(TEXT("critical_path"), CriticalPath));
	if (CriticalPath && CriticalPath->Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstCritical = (*CriticalPath)[0]->AsObject();
		FString NodeId;
		FirstCritical->TryGetStringField(TEXT("node_id"), NodeId);
		TestEqual(TEXT("Critical path top node should be Parent"), NodeId, FString(TEXT("Parent-GUID")));
	}

	FString CriticalPathMethod;
	TestTrue(TEXT("critical_path_method exists"), RootObject->TryGetStringField(TEXT("critical_path_method"), CriticalPathMethod));
	TestEqual(TEXT("critical_path_method should be strict mode"), CriticalPathMethod, FString(TEXT("dag_longest_path_self_weighted")));

	const TArray<TSharedPtr<FJsonValue>>* LinkGraph = nullptr;
	TestTrue(TEXT("link_contribution_graph array exists"), RootObject->TryGetArrayField(TEXT("link_contribution_graph"), LinkGraph));
	if (LinkGraph && LinkGraph->Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstEdge = (*LinkGraph)[0]->AsObject();
		FString ParentNodeId;
		FString ChildNodeId;
		double TotalChildInclusive = 0.0;
		FirstEdge->TryGetStringField(TEXT("parent_node_id"), ParentNodeId);
		FirstEdge->TryGetStringField(TEXT("child_node_id"), ChildNodeId);
		FirstEdge->TryGetNumberField(TEXT("total_child_inclusive_ms"), TotalChildInclusive);
		TestEqual(TEXT("First edge parent should be Parent"), ParentNodeId, FString(TEXT("Parent-GUID")));
		TestEqual(TEXT("First edge child should be Child"), ChildNodeId, FString(TEXT("Child-GUID")));
		TestTrue(TEXT("First edge total child inclusive should be 7"), FMath::IsNearlyEqual(TotalChildInclusive, 7.0, 0.001));
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteFile(*SavedPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGProfilerExportJsonSchemaTest, "Plugins.PCGProfiler.Export.JsonSchema", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGProfilerExportJsonSchemaTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("GEngine is null."));
		return false;
	}

	UPCGProfilerSubsystem* Subsystem = GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>();
	if (!Subsystem)
	{
		AddError(TEXT("UPCGProfilerSubsystem is unavailable."));
		return false;
	}

	Subsystem->SetStrictCriticalPathMode(false);
	Subsystem->StartRun(TEXT("Automation_Export"));

	FPCGProfilerNodeEvent Parent;
	Parent.NodeId = TEXT("ParentNode");
	Parent.NodeTitle = TEXT("ParentNode");
	Parent.GraphName = TEXT("GraphExport");
	Parent.Phase = TEXT("PrepareData");
	Parent.ExecutionPath = TEXT("/Root/ParentNode");
	Parent.InclusiveMs = 8.0;
	Parent.SelfMs = 8.0;
	Parent.DurationMs = 8.0;
	Parent.InputPoints = 30;
	Parent.OutputPoints = 20;
	Subsystem->RecordNodeEvent(Parent);

	FPCGProfilerNodeEvent Child;
	Child.NodeId = TEXT("ChildNode");
	Child.NodeTitle = TEXT("ChildNode");
	Child.GraphName = TEXT("GraphExport");
	Child.Phase = TEXT("Execute");
	Child.ExecutionPath = TEXT("/Root/ParentNode/ChildNode");
	Child.ParentExecutionPath = TEXT("/Root/ParentNode");
	Child.InclusiveMs = 5.0;
	Child.SelfMs = 5.0;
	Child.DurationMs = 5.0;
	Child.InputPoints = 15;
	Child.OutputPoints = 10;
	Subsystem->RecordNodeEvent(Child);

	FPCGProfilerNodeEvent Post = Child;
	Post.Phase = TEXT("PostExecute");
	Post.InclusiveMs = 2.0;
	Post.SelfMs = 2.0;
	Post.DurationMs = 2.0;
	Post.InputPoints = 12;
	Post.OutputPoints = 8;
	Subsystem->RecordNodeEvent(Post);

	FString SavedPath;
	const bool bExported = Subsystem->ExportJsonReport(TEXT("Tests/PCGProfiler/AutomationReport.json"), SavedPath);
	TestTrue(TEXT("ExportJsonReport should succeed"), bExported);
	if (!bExported)
	{
		return false;
	}

	FString JsonText;
	const bool bLoaded = FFileHelper::LoadFileToString(JsonText, *SavedPath);
	TestTrue(TEXT("Exported JSON file should be readable"), bLoaded);
	if (!bLoaded)
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Exported JSON should parse"), bParsed);
	if (!bParsed)
	{
		return false;
	}

	TestTrue(TEXT("JSON has nodes"), RootObject->HasField(TEXT("nodes")));
	TestTrue(TEXT("JSON has events"), RootObject->HasField(TEXT("events")));
	TestTrue(TEXT("JSON has phase_buckets"), RootObject->HasField(TEXT("phase_buckets")));
	TestTrue(TEXT("JSON has top_n"), RootObject->HasField(TEXT("top_n")));
	TestTrue(TEXT("JSON has cross_run_node_stats"), RootObject->HasField(TEXT("cross_run_node_stats")));
	TestTrue(TEXT("JSON has critical_path"), RootObject->HasField(TEXT("critical_path")));
	TestTrue(TEXT("JSON has critical_path_method"), RootObject->HasField(TEXT("critical_path_method")));
	TestTrue(TEXT("JSON has link_contribution_graph"), RootObject->HasField(TEXT("link_contribution_graph")));
	TestTrue(TEXT("JSON has thread_load"), RootObject->HasField(TEXT("thread_load")));
	TestTrue(TEXT("JSON has cache_analysis"), RootObject->HasField(TEXT("cache_analysis")));
	TestTrue(TEXT("JSON has scale_summary"), RootObject->HasField(TEXT("scale_summary")));

	const TSharedPtr<FJsonObject>* ThreadLoad = nullptr;
	TestTrue(TEXT("thread_load object exists"), RootObject->TryGetObjectField(TEXT("thread_load"), ThreadLoad) && ThreadLoad && ThreadLoad->IsValid());
	if (ThreadLoad && ThreadLoad->IsValid())
	{
		TestTrue(TEXT("thread_load has by_phase"), (*ThreadLoad)->HasField(TEXT("by_phase")));
		TestTrue(TEXT("thread_load has timeline_windows"), (*ThreadLoad)->HasField(TEXT("timeline_windows")));
		TestTrue(TEXT("thread_load has concurrency"), (*ThreadLoad)->HasField(TEXT("concurrency")));
		TestTrue(TEXT("thread_load has imbalance"), (*ThreadLoad)->HasField(TEXT("imbalance")));
		TestTrue(TEXT("thread_load has top_nodes_by_thread"), (*ThreadLoad)->HasField(TEXT("top_nodes_by_thread")));
		TestTrue(TEXT("thread_load has per_run_summary"), (*ThreadLoad)->HasField(TEXT("per_run_summary")));
		TestTrue(TEXT("thread_load has worker_parallelism"), (*ThreadLoad)->HasField(TEXT("worker_parallelism")));
		TestTrue(TEXT("thread_load has queue_wait_vs_execution"), (*ThreadLoad)->HasField(TEXT("queue_wait_vs_execution")));
		TestTrue(TEXT("thread_load has thread_jitter"), (*ThreadLoad)->HasField(TEXT("thread_jitter")));
		TestTrue(TEXT("thread_load has scheduling_bottlenecks"), (*ThreadLoad)->HasField(TEXT("scheduling_bottlenecks")));
		TestTrue(TEXT("thread_load has parallel_efficiency"), (*ThreadLoad)->HasField(TEXT("parallel_efficiency")));
	}

	const TSharedPtr<FJsonObject>* CacheAnalysis = nullptr;
	TestTrue(TEXT("cache_analysis object exists"), RootObject->TryGetObjectField(TEXT("cache_analysis"), CacheAnalysis) && CacheAnalysis && CacheAnalysis->IsValid());
	if (CacheAnalysis && CacheAnalysis->IsValid())
	{
		TestTrue(TEXT("cache_analysis has graph_cache_hit_count"), (*CacheAnalysis)->HasField(TEXT("graph_cache_hit_count")));
		TestTrue(TEXT("cache_analysis has graph_cache_miss_count"), (*CacheAnalysis)->HasField(TEXT("graph_cache_miss_count")));
		TestTrue(TEXT("cache_analysis has graph_cache_hit_rate"), (*CacheAnalysis)->HasField(TEXT("graph_cache_hit_rate")));
		TestTrue(TEXT("cache_analysis has cache_miss_node_ranking"), (*CacheAnalysis)->HasField(TEXT("cache_miss_node_ranking")));
		TestTrue(TEXT("cache_analysis has miss_reason_breakdown"), (*CacheAnalysis)->HasField(TEXT("miss_reason_breakdown")));
		TestTrue(TEXT("cache_analysis has cold_vs_hot_start"), (*CacheAnalysis)->HasField(TEXT("cold_vs_hot_start")));
		TestTrue(TEXT("cache_analysis has cache_invalidation_propagation_chain"), (*CacheAnalysis)->HasField(TEXT("cache_invalidation_propagation_chain")));
		TestTrue(TEXT("cache_analysis has parameter_sensitivity_report"), (*CacheAnalysis)->HasField(TEXT("parameter_sensitivity_report")));
	}

	const TSharedPtr<FJsonObject>* ScaleSummary = nullptr;
	TestTrue(TEXT("scale_summary object exists"), RootObject->TryGetObjectField(TEXT("scale_summary"), ScaleSummary) && ScaleSummary && ScaleSummary->IsValid());
	if (ScaleSummary && ScaleSummary->IsValid())
	{
		TestTrue(TEXT("scale_summary has total_input_point_count"), (*ScaleSummary)->HasField(TEXT("total_input_point_count")));
		TestTrue(TEXT("scale_summary has total_output_point_count"), (*ScaleSummary)->HasField(TEXT("total_output_point_count")));
		TestTrue(TEXT("scale_summary has total_estimated_memory_bytes"), (*ScaleSummary)->HasField(TEXT("total_estimated_memory_bytes")));
		TestTrue(TEXT("scale_summary has top_nodes_by_estimated_memory"), (*ScaleSummary)->HasField(TEXT("top_nodes_by_estimated_memory")));
		TestTrue(TEXT("scale_summary has by_phase"), (*ScaleSummary)->HasField(TEXT("by_phase")));
		TestTrue(TEXT("scale_summary has by_thread_group"), (*ScaleSummary)->HasField(TEXT("by_thread_group")));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	RootObject->TryGetArrayField(TEXT("nodes"), Nodes);
	TestTrue(TEXT("Nodes array should be non-empty"), Nodes && Nodes->Num() > 0);
	if (Nodes && Nodes->Num() > 0)
	{
		TSharedPtr<FJsonObject> ParentNodeObj;
		const bool bFoundParentNode = PCGProfilerAutomation::FindObjectByStringField(*Nodes, TEXT("node_id"), TEXT("ParentNode"), ParentNodeObj);
		TestTrue(TEXT("Parent node object exists"), bFoundParentNode);
		if (bFoundParentNode && ParentNodeObj.IsValid())
		{
			double InputPointsMax = 0.0;
			double OutputPointsMax = 0.0;
			double EstimatedMemoryBytes = 0.0;
			ParentNodeObj->TryGetNumberField(TEXT("input_points_max"), InputPointsMax);
			ParentNodeObj->TryGetNumberField(TEXT("output_points_max"), OutputPointsMax);
			ParentNodeObj->TryGetNumberField(TEXT("estimated_memory_bytes"), EstimatedMemoryBytes);
			TestTrue(TEXT("Parent input_points_max should be 30"), FMath::IsNearlyEqual(InputPointsMax, 30.0, 0.001));
			TestTrue(TEXT("Parent output_points_max should be 20"), FMath::IsNearlyEqual(OutputPointsMax, 20.0, 0.001));
			TestTrue(TEXT("Parent estimated_memory_bytes should be 8000"), FMath::IsNearlyEqual(EstimatedMemoryBytes, 8000.0, 0.001));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	RootObject->TryGetArrayField(TEXT("events"), Events);
	TestTrue(TEXT("Events array should be non-empty"), Events && Events->Num() > 0);
	if (Events && Events->Num() > 0)
	{
		const TSharedPtr<FJsonObject> FirstEventObj = (*Events)[0]->AsObject();
		TestTrue(TEXT("First event has input_points"), FirstEventObj.IsValid() && FirstEventObj->HasField(TEXT("input_points")));
		TestTrue(TEXT("First event has output_points"), FirstEventObj.IsValid() && FirstEventObj->HasField(TEXT("output_points")));
		TestTrue(TEXT("First event has estimated_memory_bytes"), FirstEventObj.IsValid() && FirstEventObj->HasField(TEXT("estimated_memory_bytes")));
		TestTrue(TEXT("First event has prepare_data_ms"), FirstEventObj.IsValid() && FirstEventObj->HasField(TEXT("prepare_data_ms")));
		TestTrue(TEXT("First event has post_execute_ms"), FirstEventObj.IsValid() && FirstEventObj->HasField(TEXT("post_execute_ms")));
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteFile(*SavedPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGProfilerProjectIntegrationTest, "Plugins.PCGProfiler.ProjectIntegration.ElectricDreams", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCGProfilerProjectIntegrationTest::RunTest(const FString& Parameters)
{
	if (!GEngine)
	{
		AddError(TEXT("GEngine is null."));
		return false;
	}

	UPCGProfilerSubsystem* Subsystem = GEngine->GetEngineSubsystem<UPCGProfilerSubsystem>();
	if (!Subsystem)
	{
		AddError(TEXT("UPCGProfilerSubsystem is unavailable."));
		return false;
	}

	FString PCGMapPath;
	const bool bHasPCGMap = FPackageName::SearchForPackageOnDisk(TEXT("/Game/Levels/PCG/ElectricDreams_PCG"), &PCGMapPath);
	TestTrue(TEXT("ElectricDreams PCG map should exist"), bHasPCGMap);

	FString PCGGraphPath;
	const bool bHasPCGGraph = FPackageName::SearchForPackageOnDisk(TEXT("/Game/PCG/Graphs/Forest/PCGDemo_Forest"), &PCGGraphPath);
	TestTrue(TEXT("ElectricDreams PCG graph should exist"), bHasPCGGraph);

	Subsystem->SetStrictCriticalPathMode(true);
	Subsystem->StartRun(TEXT("Automation_ProjectIntegration"));

	FPCGProfilerNodeEvent EventA;
	EventA.NodeId = TEXT("ProjectNode-A");
	EventA.NodeTitle = TEXT("ProjectNode-A");
	EventA.GraphName = TEXT("ElectricDreams_PCG");
	EventA.ExecutionPath = TEXT("/Project/A");
	EventA.Phase = TEXT("Execute");
	EventA.ThreadGroup = TEXT("GameThread");
	EventA.InclusiveMs = 5.0;
	EventA.SelfMs = 4.0;
	EventA.DurationMs = 5.0;
	EventA.ExecutionMs = 4.0;
	EventA.QueueWaitMs = 1.0;
	EventA.FirstSeenTimeMs = 1.0;
	Subsystem->RecordNodeEvent(EventA);

	FPCGProfilerNodeEvent EventB = EventA;
	EventB.NodeId = TEXT("ProjectNode-B");
	EventB.NodeTitle = TEXT("ProjectNode-B");
	EventB.ExecutionPath = TEXT("/Project/A/B");
	EventB.ParentExecutionPath = TEXT("/Project/A");
	EventB.ThreadGroup = TEXT("Worker");
	EventB.InclusiveMs = 3.0;
	EventB.SelfMs = 3.0;
	EventB.DurationMs = 3.0;
	EventB.ExecutionMs = 2.5;
	EventB.QueueWaitMs = 0.5;
	EventB.FirstSeenTimeMs = 2.0;
	Subsystem->RecordNodeEvent(EventB);

	FString SavedPath;
	const bool bExported = Subsystem->ExportJsonReport(TEXT("Tests/PCGProfiler/AutomationProjectIntegration.json"), SavedPath);
	TestTrue(TEXT("Project integration export should succeed"), bExported);
	if (!bExported)
	{
		return false;
	}

	FString JsonText;
	const bool bLoaded = FFileHelper::LoadFileToString(JsonText, *SavedPath);
	TestTrue(TEXT("Project integration JSON should be readable"), bLoaded);
	if (!bLoaded)
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	const bool bParsed = FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid();
	TestTrue(TEXT("Project integration JSON should parse"), bParsed);
	if (!bParsed)
	{
		return false;
	}

	TestTrue(TEXT("JSON has thread_load"), RootObject->HasField(TEXT("thread_load")));
	TestTrue(TEXT("JSON has critical_path_method"), RootObject->HasField(TEXT("critical_path_method")));
	TestTrue(TEXT("JSON has link_contribution_graph"), RootObject->HasField(TEXT("link_contribution_graph")));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.DeleteFile(*SavedPath);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
