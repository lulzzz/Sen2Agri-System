#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <fstream>

#include "lairetrievalhandler.hpp"
#include "processorhandlerhelper.h"
#include "json_conversions.hpp"

// The number of tasks that are executed for each product before executing time series tasks
#define LAI_TASKS_PER_PRODUCT       4
#define MODEL_GEN_TASKS_PER_PRODUCT 4

#define DEFAULT_GENERATED_SAMPLES_NO    "40000"
#define DEFAULT_NOISE_VAR               "0.01"
#define DEFAULT_BEST_OF                 "1"
#define DEFAULT_REGRESSOR               "nn"

void LaiRetrievalHandler::CreateTasksForNewProducts(QList<TaskToSubmit> &outAllTasksList,
                                                    QList<std::reference_wrapper<const TaskToSubmit>> &outProdFormatterParentsList,
                                                     int nbProducts, bool bGenModels, bool bNDayReproc, bool bFittedReproc) {
    // just create the tasks but with no information so far
    // first we add the tasks to be performed for each product
    int TasksNoPerProduct = LAI_TASKS_PER_PRODUCT;
    if(bGenModels)
        TasksNoPerProduct += MODEL_GEN_TASKS_PER_PRODUCT;
    for(int i = 0; i<nbProducts; i++) {
        if(bGenModels) {
            outAllTasksList.append(TaskToSubmit{ "lai-bv-input-variable-generation", {} });
            outAllTasksList.append(TaskToSubmit{ "lai-prosail-simulator", {} });
            outAllTasksList.append(TaskToSubmit{ "lai-training-data-generator", {} });
            outAllTasksList.append(TaskToSubmit{ "lai-inverse-model-learning", {} });
        }
        outAllTasksList.append(TaskToSubmit{"lai-ndvi-rvi-extractor", {}});
        outAllTasksList.append(TaskToSubmit{"lai-bv-image-invertion", {}});
        outAllTasksList.append(TaskToSubmit{"lai-bv-err-image-invertion", {}});
        outAllTasksList.append(TaskToSubmit{"lai-mono-date-mask-flags", {}});
    }
    if(bNDayReproc || bFittedReproc) {
        outAllTasksList.append(TaskToSubmit{"lai-time-series-builder", {}});
        outAllTasksList.append(TaskToSubmit{"lai-err-time-series-builder", {}});
        outAllTasksList.append(TaskToSubmit{"lai-msk-flags-time-series-builder", {}});
        if(bNDayReproc) {
            outAllTasksList.append(TaskToSubmit{"lai-local-window-reprocessing", {}});
            outAllTasksList.append(TaskToSubmit{"lai-local-window-reproc-splitter", {}});
        }
        if(bFittedReproc) {
            outAllTasksList.append(TaskToSubmit{"lai-fitted-reprocessing", {}});
            outAllTasksList.append(TaskToSubmit{"lai-fitted-reproc-splitter", {}});
        }
    }
    //outAllTasksList.append(TaskToSubmit{"product-formatter", {}});

    // now fill the tasks hierarchy infos

    //   ----------------------------- LOOP --------------------------------------------
    //   |                                                                              |
    //   |                      bv-input-variable-generation   (optional)               |
    //   |                              |                                               |
    //   |                      prosail-simulator              (optional)               |
    //   |                              |                                               |
    //   |                      training-data-generator        (optional)               |
    //   |                              |                                               |
    //   |                      inverse-model-learning         (optional)               |
    //   |                              |                                               |
    //   |                      ndvi-rvi-extraction                                     |
    //   |                              |                                               |
    //   |              ---------------------------------------------------             |
    //   |              |                      |                           |            |
    //   |      bv-image-inversion     bv-err-image-inversion   lai-mono-date-mask-flags|
    //   |              |                      |                           |            |
    //   |              ---------------------------------------------------             |
    //   |                              |                                               |
    //   -------------------------------------------------------------------------------
    //                                  |
    //              ---------------------------------------------------------------------------------
    //              |                              |                              |                 |
    //      time-series-builder         err-time-series-builder   lai-msk-flags-time-series-builder |
    //              |                              |                              |                 |
    //              ---------------------------------------------------------------------------------
    //                                  |
    //              ---------------------------------------------
    //              |                                           |
    //      profile-reprocessing                fitted-profile-reprocessing
    //              |                                           |
    //      reprocessed-profile-splitter        fitted-reprocessed-profile-splitter
    //              |                                           |
    //              ---------------------------------------------
    //                                  |
    //                          product-formatter
    //
    // NOTE: In this moment, the products in loop are not executed in parallel. To do this, the if(i > 0) below
    //      should be removed but in this case, the time-series-builders should wait for all the monodate images
    int i;
    QList<int> bvImageInvIdxs;
    QList<int> bvErrImageInvIdxs;
    QList<int> laiMonoDateFlgsIdxs;
    // we execute in parallel and launch at once all processing chains for each product
    // for example, if we have genModels, we launch all bv-input-variable-generation for all products
    // if we do not have genModels, we launch all NDVIRVIExtraction in the same time for all products
    for(i = 0; i<nbProducts; i++) {
        int loopFirstIdx = i*TasksNoPerProduct;
        // initialize the ndviRvi task index
        int ndviRviExtrIdx = loopFirstIdx;
        // add the tasks for generating models
        if(bGenModels) {
            int prosailSimulatorIdx = loopFirstIdx+1;
            outAllTasksList[prosailSimulatorIdx].parentTasks.append(outAllTasksList[loopFirstIdx]);
            outAllTasksList[prosailSimulatorIdx+1].parentTasks.append(outAllTasksList[prosailSimulatorIdx]);
            outAllTasksList[prosailSimulatorIdx+2].parentTasks.append(outAllTasksList[prosailSimulatorIdx+1]);
            // now update the index for the ndviRvi task and set its parent to the inverse-model-learning task
            ndviRviExtrIdx += MODEL_GEN_TASKS_PER_PRODUCT;
            outAllTasksList[ndviRviExtrIdx].parentTasks.append(outAllTasksList[prosailSimulatorIdx+2]);
        }
        // the others comme naturally updated
        // bv-image-inversion -> ndvi-rvi-extraction
        int nBVImageInversionIdx = ndviRviExtrIdx+1;
        outAllTasksList[nBVImageInversionIdx].parentTasks.append(outAllTasksList[ndviRviExtrIdx]);
        bvImageInvIdxs.append(nBVImageInversionIdx);

        // bv-err-image-inversion -> ndvi-rvi-extraction
        int nBVErrImageInversionIdx = nBVImageInversionIdx+1;
        outAllTasksList[nBVErrImageInversionIdx].parentTasks.append(outAllTasksList[ndviRviExtrIdx]);
        bvErrImageInvIdxs.append(nBVErrImageInversionIdx);

        // lai-mono-date-mask-flags -> ndvi-rvi-extraction
        int nLaiMonoDateFlgsIdx = nBVErrImageInversionIdx+1;
        outAllTasksList[nLaiMonoDateFlgsIdx].parentTasks.append(outAllTasksList[ndviRviExtrIdx]);
        laiMonoDateFlgsIdxs.append(nLaiMonoDateFlgsIdx);

        // add the parent tasks for the product formatter, if it is the case
//        if(!bNDayReproc && !bFittedReproc) {
//            for(int j = 0; j<3; j++) {
//                outProdFormatterParentsList.append(outAllTasksList[ndviRviExtrIdx+j+1]);
//            }
//        }
    }
    int nCurIdx = i*TasksNoPerProduct;
    if(bNDayReproc || bFittedReproc) {
        // time-series-builder -> last bv-image-inversion AND bv-err-image-inversion
        m_nTimeSeriesBuilderIdx = nCurIdx++;
        //int nPrevBvErrImgInvIdx = (i-1)*TasksNoPerProduct + (TasksNoPerProduct-1);
        for(int idx: bvImageInvIdxs) {
            outAllTasksList[m_nTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[idx]);
        }
        //outAllTasksList[m_nTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx-1]);
        //outAllTasksList[m_nTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx]);

        //err-time-series-builder -> last bv-image-inversion AND bv-err-image-inversion
        m_nErrTimeSeriesBuilderIdx = nCurIdx++;
        for(int idx: bvErrImageInvIdxs) {
            outAllTasksList[m_nErrTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[idx]);
        }

        //outAllTasksList[m_nErrTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx-1]);
        //outAllTasksList[m_nErrTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx]);

        //lai-msk-flags-time-series-builder -> last bv-image-inversion AND bv-err-image-inversion
        m_nLaiMskFlgsTimeSeriesBuilderIdx = nCurIdx++;
        for(int idx: laiMonoDateFlgsIdxs) {
            outAllTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[idx]);
        }

        //outAllTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx-1]);
        //outAllTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx]);

        if(bNDayReproc) {
            //profile-reprocessing -> time-series-builder AND err-time-series-builder AND lai-msk-flags-time-series-builder
            m_nProfileReprocessingIdx = nCurIdx++;
            outAllTasksList[m_nProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nTimeSeriesBuilderIdx]);
            outAllTasksList[m_nProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nErrTimeSeriesBuilderIdx]);
            outAllTasksList[m_nProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx]);

            //reprocessed-profile-splitter -> profile-reprocessing
            m_nReprocessedProfileSplitterIdx = nCurIdx++;
            outAllTasksList[m_nReprocessedProfileSplitterIdx].parentTasks.append(outAllTasksList[m_nProfileReprocessingIdx]);
        }

        if(bFittedReproc) {
            //fitted-profile-reprocessing -> time-series-builder AND err-time-series-builder AND lai-msk-flags-time-series-builder
            m_nFittedProfileReprocessingIdx = nCurIdx++;
            outAllTasksList[m_nFittedProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nTimeSeriesBuilderIdx]);
            outAllTasksList[m_nFittedProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nErrTimeSeriesBuilderIdx]);
            outAllTasksList[m_nFittedProfileReprocessingIdx].parentTasks.append(outAllTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx]);

            //fitted-reprocessed-profile-splitter -> fitted-profile-reprocessing
            m_nFittedProfileReprocessingSplitterIdx = nCurIdx++;
            outAllTasksList[m_nFittedProfileReprocessingSplitterIdx].parentTasks.append(outAllTasksList[m_nFittedProfileReprocessingIdx]);
        }
        //product-formatter -> reprocessed-profile-splitter OR fitted-reprocessed-profile-splitter (OR BOTH)
//        m_nProductFormatterIdx = nCurIdx;
        if(bNDayReproc) {
            outProdFormatterParentsList.append(outAllTasksList[m_nReprocessedProfileSplitterIdx]);
//            outAllTasksList[m_nProductFormatterIdx].parentTasks.append(outAllTasksList[m_nReprocessedProfileSplitterIdx]);
        }
        if(bFittedReproc) {
            outProdFormatterParentsList.append(outAllTasksList[m_nFittedProfileReprocessingSplitterIdx]);
//            outAllTasksList[m_nProductFormatterIdx].parentTasks.append(outAllTasksList[m_nFittedProfileReprocessingSplitterIdx]);
        }
    } else {
        //product-formatter -> ALL bv-image-inversion AND bv-err-image-inversion and lai-mono-date-mask-flags
        //m_nProductFormatterIdx = nCurIdx;
        for(int idx: bvImageInvIdxs) {
            outProdFormatterParentsList.append(outAllTasksList[idx]);
        }
        for(int idx: bvErrImageInvIdxs) {
            outProdFormatterParentsList.append(outAllTasksList[idx]);
        }
        for(int idx: laiMonoDateFlgsIdxs) {
            outProdFormatterParentsList.append(outAllTasksList[idx]);
        }
//        int nPrevBvErrImgInvIdx = (i-1)*TasksNoPerProduct + (TasksNoPerProduct-2);
//        outProdFormatterParentsList.append(outAllTasksList[nPrevBvErrImgInvIdx-2]);
//        outProdFormatterParentsList.append(outAllTasksList[nPrevBvErrImgInvIdx-1]);
//        outProdFormatterParentsList.append(outAllTasksList[nPrevBvErrImgInvIdx]);
        //outAllTasksList[m_nProductFormatterIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx-1]);
        //outAllTasksList[m_nProductFormatterIdx].parentTasks.append(outAllTasksList[nPrevBvErrImgInvIdx]);
    }
}

LAIGlobalExecutionInfos LaiRetrievalHandler::HandleNewTilesList(EventProcessingContext &ctx, const JobSubmittedEvent &event,
                                                const QStringList &listProducts) {

    const QJsonObject &parameters = QJsonDocument::fromJson(event.parametersJson.toUtf8()).object();
    std::map<QString, QString> configParameters = ctx.GetJobConfigurationParameters(event.jobId, "processor.l3b.lai.");

    // Get the resolution value
    const auto &resolution = QString::number(parameters["resolution"].toInt());

    bool bGenModels = false;
    if(parameters.contains("genmodel")) {
        bGenModels = (parameters["genmodel"].toInt() != 0);
    } else {
        bGenModels = ((configParameters["processor.l3b.generate_models"]).toInt() != 0);
    }

    /*bool bMonoDateLai = false;
    if(parameters.contains("monolai")) {
        bMonoDateLai = (parameters["monolai"].toInt() != 0);
    } else {
        bMonoDateLai = ((configParameters["processor.l3b.mono_date_lai"]).toInt() != 0);
    }
    */

    bool bNDayReproc = false;
    if(parameters.contains("reproc")) {
        bNDayReproc = (parameters["reproc"].toInt() != 0);
    } else {
        bNDayReproc = ((configParameters["processor.l3b.reprocess"]).toInt() != 0);
    }

    bool bFittedReproc = false;
    if(parameters.contains("fitted")) {
        bFittedReproc = (parameters["fitted"].toInt() != 0);
    } else {
        bFittedReproc = ((configParameters["processor.l3b.fitted"]).toInt() != 0);
    }


    // The returning value
    LAIGlobalExecutionInfos globalExecInfos;
    QList<TaskToSubmit> &allTasksList = globalExecInfos.allTasksList;
    QList<std::reference_wrapper<const TaskToSubmit>> &prodFormParTsksList = globalExecInfos.prodFormatParams.parentsTasksRef;
    CreateTasksForNewProducts(allTasksList, prodFormParTsksList, listProducts.size(), bGenModels, bNDayReproc, bFittedReproc);

    QList<std::reference_wrapper<TaskToSubmit>> allTasksListRef;
    for(TaskToSubmit &task: allTasksList) {
        allTasksListRef.append(task);
    }
    // submit all tasks
    ctx.SubmitTasks(event.jobId, allTasksListRef);

    NewStepList &steps = globalExecInfos.allStepsList;
    QStringList ndviFileNames;
    QStringList monoDateLaiFileNames;
    QStringList monoDateErrLaiFileNames;
    QStringList monoDateMskFlagsLaiFileNames;
    // first extract the model file names from the models folder
    int TasksNoPerProduct = LAI_TASKS_PER_PRODUCT;
    if(bGenModels) {
        GetStepsToGenModel(configParameters, listProducts, allTasksList, steps);
        TasksNoPerProduct += MODEL_GEN_TASKS_PER_PRODUCT;
    }

    const auto &modelsFolder = configParameters["processor.l3b.lai.modelsfolder"];

    int i;
    for (i = 0; i<listProducts.size(); i++) {
        const auto &inputProduct = listProducts[i];
        // initialize the ndviRvi task index
        int ndviRviExtrIdx = i*TasksNoPerProduct;
        if(bGenModels) {
            // now update the index for the ndviRvi task
            ndviRviExtrIdx += MODEL_GEN_TASKS_PER_PRODUCT;
        }
        TaskToSubmit &ndviRviExtractorTask = allTasksList[ndviRviExtrIdx];
        TaskToSubmit &bvImageInversionTask = allTasksList[ndviRviExtrIdx+1];
        TaskToSubmit &bvErrImageInversionTask = allTasksList[ndviRviExtrIdx+2];
        TaskToSubmit &genMonoDateMskFagsTask = allTasksList[ndviRviExtrIdx+3];

        const auto & singleNdviFile = ndviRviExtractorTask.GetFilePath("single_ndvi.tif");
        const auto & ftsFile = ndviRviExtractorTask.GetFilePath("ndvi_rvi.tif");
        const auto & monoDateLaiFileName = bvImageInversionTask.GetFilePath("LAI_mono_date_img.tif");
        const auto & monoDateErrFileName = bvErrImageInversionTask.GetFilePath("LAI_mono_date_ERR_img.tif");
        const auto & monoDateMskFlgsFileName = genMonoDateMskFagsTask.GetFilePath("LAI_mono_date_msk_flgs_img.tif");

        // save the mono date LAI file name list
        ndviFileNames.append(singleNdviFile);
        monoDateLaiFileNames.append(monoDateLaiFileName);
        monoDateErrLaiFileNames.append(monoDateErrFileName);
        monoDateMskFlagsLaiFileNames.append(monoDateMskFlgsFileName);

        QStringList ndviRviExtractionArgs = GetNdviRviExtractionArgs(inputProduct, ftsFile, singleNdviFile, resolution);
        QStringList bvImageInvArgs = GetBvImageInvArgs(ftsFile, inputProduct, modelsFolder, monoDateLaiFileName);
        QStringList bvErrImageInvArgs = GetBvErrImageInvArgs(ftsFile, inputProduct, modelsFolder, monoDateErrFileName);
        QStringList genMonoDateMskFagsArgs = GetMonoDateMskFagsArgs(inputProduct, monoDateMskFlgsFileName);

        // add these steps to the steps list to be submitted
        steps.append(ndviRviExtractorTask.CreateStep("NdviRviExtraction2", ndviRviExtractionArgs));
        steps.append(bvImageInversionTask.CreateStep("BVImageInversion", bvImageInvArgs));
        steps.append(bvErrImageInversionTask.CreateStep("BVImageInversion", bvErrImageInvArgs));
        steps.append(genMonoDateMskFagsTask.CreateStep("GenerateLaiMonoDateMaskFlags", genMonoDateMskFagsArgs));
    }

    QString fittedFileListFileName;
    QString fittedFlagsFileListFileName;
    QString reprocFileListFileName;
    QString reprocFlagsFileListFileName;

    if(bNDayReproc || bFittedReproc) {
        TaskToSubmit &imgTimeSeriesBuilderTask = allTasksList[m_nTimeSeriesBuilderIdx];
        TaskToSubmit &errTimeSeriesBuilderTask = allTasksList[m_nErrTimeSeriesBuilderIdx];
        TaskToSubmit &mskFlagsTimeSeriesBuilderTask = allTasksList[m_nLaiMskFlgsTimeSeriesBuilderIdx];

        const auto & allLaiTimeSeriesFileName = imgTimeSeriesBuilderTask.GetFilePath("LAI_time_series.tif");
        const auto & allErrTimeSeriesFileName = errTimeSeriesBuilderTask.GetFilePath("Err_time_series.tif");
        const auto & allMskFlagsTimeSeriesFileName = mskFlagsTimeSeriesBuilderTask.GetFilePath("Mask_Flags_time_series.tif");

        QStringList timeSeriesBuilderArgs = GetTimeSeriesBuilderArgs(monoDateLaiFileNames, allLaiTimeSeriesFileName);
        QStringList errTimeSeriesBuilderArgs = GetErrTimeSeriesBuilderArgs(monoDateErrLaiFileNames, allErrTimeSeriesFileName);
        QStringList mskFlagsTimeSeriesBuilderArgs = GetMskFlagsTimeSeriesBuilderArgs(monoDateMskFlagsLaiFileNames, allMskFlagsTimeSeriesFileName);

        steps.append(imgTimeSeriesBuilderTask.CreateStep("TimeSeriesBuilder", timeSeriesBuilderArgs));
        steps.append(errTimeSeriesBuilderTask.CreateStep("TimeSeriesBuilder", errTimeSeriesBuilderArgs));
        steps.append(mskFlagsTimeSeriesBuilderTask.CreateStep("TimeSeriesBuilder", mskFlagsTimeSeriesBuilderArgs));

        if(bNDayReproc) {
            TaskToSubmit &profileReprocTask = allTasksList[m_nProfileReprocessingIdx];
            TaskToSubmit &profileReprocSplitTask = allTasksList[m_nReprocessedProfileSplitterIdx];

            const auto & reprocTimeSeriesFileName = profileReprocTask.GetFilePath("ReprocessedTimeSeries.tif");
            reprocFileListFileName = profileReprocSplitTask.GetFilePath("ReprocessedFilesList.txt");
            reprocFlagsFileListFileName = profileReprocSplitTask.GetFilePath("ReprocessedFlagsFilesList.txt");

            QStringList profileReprocessingArgs = GetProfileReprocessingArgs(configParameters, allLaiTimeSeriesFileName,
                                                                             allErrTimeSeriesFileName, allMskFlagsTimeSeriesFileName,
                                                                             reprocTimeSeriesFileName, listProducts);
            QStringList reprocProfileSplitterArgs = GetReprocProfileSplitterArgs(reprocTimeSeriesFileName, reprocFileListFileName,
                                                                                 reprocFlagsFileListFileName, listProducts);
            steps.append(profileReprocTask.CreateStep("ProfileReprocessing", profileReprocessingArgs));
            steps.append(profileReprocSplitTask.CreateStep("ReprocessedProfileSplitter2", reprocProfileSplitterArgs));
        }

        if(bFittedReproc) {
            TaskToSubmit &fittedProfileReprocTask = allTasksList[m_nFittedProfileReprocessingIdx];
            TaskToSubmit &fittedProfileReprocSplitTask = allTasksList[m_nFittedProfileReprocessingSplitterIdx];

            const auto & fittedTimeSeriesFileName = fittedProfileReprocTask.GetFilePath("FittedTimeSeries.tif");
            fittedFileListFileName = fittedProfileReprocSplitTask.GetFilePath("FittedFilesList.txt");
            fittedFlagsFileListFileName = fittedProfileReprocSplitTask.GetFilePath("FittedFlagsFilesList.txt");

            QStringList fittedProfileReprocArgs = GetFittedProfileReprocArgs(allLaiTimeSeriesFileName, allErrTimeSeriesFileName,
                                                                             allMskFlagsTimeSeriesFileName, fittedTimeSeriesFileName, listProducts);
            QStringList fittedProfileReprocSplitterArgs = GetFittedProfileReprocSplitterArgs(fittedTimeSeriesFileName, fittedFileListFileName,
                                                                                             fittedFlagsFileListFileName, listProducts);
            steps.append(fittedProfileReprocTask.CreateStep("ProfileReprocessing", fittedProfileReprocArgs));
            steps.append(fittedProfileReprocSplitTask.CreateStep("ReprocessedProfileSplitter2", fittedProfileReprocSplitterArgs));
        }
    }

    LAIProductFormatterParams &productFormatterParams = globalExecInfos.prodFormatParams;
    //productFormatterParams.parentsTasksRef = prodFormParTsksList;
    productFormatterParams.listNdvi = ndviFileNames;
    productFormatterParams.listLaiMonoDate = monoDateLaiFileNames;
    productFormatterParams.listLaiMonoDateErr = monoDateErrLaiFileNames;
    productFormatterParams.listLaiMonoDateFlgs = monoDateMskFlagsLaiFileNames;
    productFormatterParams.fileLaiReproc = reprocFileListFileName;
    productFormatterParams.fileLaiReprocFlgs = reprocFlagsFileListFileName;
    productFormatterParams.fileLaiFit = fittedFileListFileName;
    productFormatterParams.fileLaiFitFlgs = fittedFlagsFileListFileName;
    // Get the tile ID from the product XML name. We extract it from the first product in the list as all
    // producs should be for the same tile
    productFormatterParams.tileId = ProcessorHandlerHelper::GetTileId(listProducts);

    return globalExecInfos;
}

void LaiRetrievalHandler::WriteExecutionInfosFile(const QString &executionInfosPath,
                                               std::map<QString, QString> &configParameters,
                                               const QStringList &listProducts) {
    std::ofstream executionInfosFile;
    try
    {
        // Get the parameters from the configuration
        const auto &bwr = configParameters["processor.l3b.lai.localwnd.bwr"];
        const auto &fwr = configParameters["processor.l3b.lai.localwnd.fwr"];

        executionInfosFile.open(executionInfosPath.toStdString().c_str(), std::ofstream::out);
        executionInfosFile << "<?xml version=\"1.0\" ?>" << std::endl;
        executionInfosFile << "<metadata>" << std::endl;
        executionInfosFile << "  <General>" << std::endl;
        executionInfosFile << "  </General>" << std::endl;

        executionInfosFile << "  <ProfileReprocessing_parameters>" << std::endl;
        executionInfosFile << "    <bwr_for_algo_local_online_retrieval>" << bwr.toStdString() << "</bwr_for_algo_local_online_retrieval>" << std::endl;
        executionInfosFile << "    <fwr_for_algo_local_online_retrieval>"<< fwr.toStdString() <<"</fwr_for_algo_local_online_retrieval>" << std::endl;
        executionInfosFile << "  </ProfileReprocessing_parameters>" << std::endl;

        executionInfosFile << "  <XML_files>" << std::endl;
        for (int i = 0; i<listProducts.size(); i++) {
            executionInfosFile << "    <XML_" << std::to_string(i) << ">" << listProducts[i].toStdString()
                               << "</XML_" << std::to_string(i) << ">" << std::endl;
        }
        executionInfosFile << "  </XML_files>" << std::endl;
        executionInfosFile << "</metadata>" << std::endl;
        executionInfosFile.close();
    }
    catch(...)
    {

    }
}

void LaiRetrievalHandler::HandleJobSubmittedImpl(EventProcessingContext &ctx,
                                             const JobSubmittedEvent &event)
{
    const auto &parameters = QJsonDocument::fromJson(event.parametersJson.toUtf8()).object();
    const auto &inputProducts = parameters["input_products"].toArray();

    QStringList listProducts;
    for (const auto &inputProduct : inputProducts) {
        listProducts.append(ctx.findProductFiles(inputProduct.toString()));
    }
    if(listProducts.size() == 0) {
        ctx.MarkJobFailed(event.jobId);
        return;
    }
    QMap<QString, QStringList> mapTiles = ProcessorHandlerHelper::GroupTiles(listProducts);
    QList<LAIProductFormatterParams> listParams;

    TaskToSubmit productFormatterTask{"product-formatter", {}};
    NewStepList allSteps;
    //container for all task
    QList<TaskToSubmit> allTasksList;
    for(auto tile : mapTiles.keys())
    {
       QStringList listTemporalTiles = mapTiles.value(tile);
       LAIGlobalExecutionInfos infos = HandleNewTilesList(ctx, event, listTemporalTiles);
       listParams.append(infos.prodFormatParams);
       productFormatterTask.parentTasks += infos.prodFormatParams.parentsTasksRef;
       allTasksList.append(infos.allTasksList);
       allSteps.append(infos.allStepsList);
    }

    ctx.SubmitTasks(event.jobId, {productFormatterTask});

    // finally format the product
    QStringList productFormatterArgs = GetProductFormatterArgs(productFormatterTask, ctx, event, listProducts, listParams);

    // add these steps to the steps list to be submitted
    allSteps.append(productFormatterTask.CreateStep("ProductFormatter", productFormatterArgs));
    ctx.SubmitSteps(allSteps);
}

void LaiRetrievalHandler::HandleTaskFinishedImpl(EventProcessingContext &ctx,
                                             const TaskFinishedEvent &event)
{
    if (event.module == "product-formatter") {
        ctx.MarkJobFinished(event.jobId);

        QString prodName = GetProductFormatterProducName(ctx, event);
        QString productFolder = GetFinalProductFolder(ctx, event.jobId, event.siteId) + "/" + prodName;
        if(prodName != "") {
            QString quicklook = GetProductFormatterQuicklook(ctx, event);
            QString footPrint = GetProductFormatterFootprint(ctx, event);
            // Insert the product into the database
            ctx.InsertProduct({ ProductType::L3BLaiProductTypeId,
                                event.processorId,
                                event.siteId,
                                event.jobId,
                                productFolder,
                                QDateTime::currentDateTimeUtc(),
                                prodName,
                                quicklook,
                                footPrint,
                                TileList() });

            // Now remove the job folder containing temporary files
            //RemoveJobFolder(ctx, event.jobId);
        }
    }
}

void LaiRetrievalHandler::GetModelFileList(QStringList &outListModels, const QString &strPattern, std::map<QString, QString> &configParameters)
{
    // Get the models folder name
    const auto &modelsFolder = configParameters["processor.l3b.lai.modelsfolder"];
    QStringList nameFilters = {strPattern};
    QDirIterator it(modelsFolder, nameFilters, QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        if (QFileInfo(it.filePath()).isFile()) {
            outListModels.append(it.filePath());
        }
    }
}

QStringList LaiRetrievalHandler::GetNdviRviExtractionArgs(const QString &inputProduct, const QString &ftsFile, const QString &ndviFile,
                                                          const QString &resolution) {
    return { "NdviRviExtraction2",
           "-xml", inputProduct,
           "-ndvi", ndviFile,
           "-fts", ftsFile,
           "-outres", resolution
    };
}

QStringList LaiRetrievalHandler::GetBvImageInvArgs(const QString &ftsFile, const QString &xmlFile, const QString &modelsFolder, const QString &monoDateLaiFileName) {
    return { "BVImageInversion",
        "-in", ftsFile,
        "-out", monoDateLaiFileName,
        "-xml", xmlFile,
        "-modelsfolder", modelsFolder,
        "-modelprefix", "Model_"
    };
}

QStringList LaiRetrievalHandler::GetBvErrImageInvArgs(const QString &ftsFile, const QString &xmlFile, const QString &modelsFolder, const QString &monoDateErrFileName)  {
    return { "BVImageInversion",
        "-in", ftsFile,
        "-out", monoDateErrFileName,
        "-xml", xmlFile,
        "-modelsfolder", modelsFolder,
        "-modelprefix", "Err_Est_Model_"
    };
}

QStringList LaiRetrievalHandler::GetMonoDateMskFagsArgs(const QString &inputProduct, const QString &monoDateMskFlgsFileName) {
    return { "GenerateLaiMonoDateMaskFlags",
      "-inxml", inputProduct,
      "-out", monoDateMskFlgsFileName
    };
}


QStringList LaiRetrievalHandler::GetTimeSeriesBuilderArgs(const QStringList &monoDateLaiFileNames, const QString &allLaiTimeSeriesFileName) {
    QStringList timeSeriesBuilderArgs = { "TimeSeriesBuilder",
      "-out", allLaiTimeSeriesFileName,
      "-il"
    };
    timeSeriesBuilderArgs += monoDateLaiFileNames;

    return timeSeriesBuilderArgs;
}

QStringList LaiRetrievalHandler::GetErrTimeSeriesBuilderArgs(const QStringList &monoDateErrLaiFileNames, const QString &allErrTimeSeriesFileName) {
    QStringList timeSeriesBuilderArgs = { "TimeSeriesBuilder",
      "-out", allErrTimeSeriesFileName,
      "-il"
    };
    timeSeriesBuilderArgs += monoDateErrLaiFileNames;

    return timeSeriesBuilderArgs;
}

QStringList LaiRetrievalHandler::GetMskFlagsTimeSeriesBuilderArgs(const QStringList &monoDateMskFlagsLaiFileNames, const QString &allMskFlagsTimeSeriesFileName) {
    QStringList timeSeriesBuilderArgs = { "TimeSeriesBuilder",
      "-out", allMskFlagsTimeSeriesFileName,
      "-il"
    };
    timeSeriesBuilderArgs += monoDateMskFlagsLaiFileNames;

    return timeSeriesBuilderArgs;
}

QStringList LaiRetrievalHandler::GetProfileReprocessingArgs(std::map<QString, QString> configParameters, const QString &allLaiTimeSeriesFileName,
                                       const QString &allErrTimeSeriesFileName, const QString &allMsksTimeSeriesFileName,
                                       const QString &reprocTimeSeriesFileName, const QStringList &listProducts) {
    const auto &localWindowBwr = configParameters["processor.l3b.lai.localwnd.bwr"];
    const auto &localWindowFwr = configParameters["processor.l3b.lai.localwnd.fwr"];

    QStringList profileReprocessingArgs = { "ProfileReprocessing",
          "-lai", allLaiTimeSeriesFileName,
          "-err", allErrTimeSeriesFileName,
          "-msks", allMsksTimeSeriesFileName,
          "-opf", reprocTimeSeriesFileName,
          "-algo", "local",
          "-algo.local.bwr", localWindowBwr,
          "-algo.local.fwr", localWindowFwr,
          "-ilxml"
    };
    profileReprocessingArgs += listProducts;
    return profileReprocessingArgs;
}

QStringList LaiRetrievalHandler::GetReprocProfileSplitterArgs(const QString &reprocTimeSeriesFileName, const QString &reprocFileListFileName,
                                                              const QString &reprocFlagsFileListFileName,
                                                              const QStringList &allXmlsFileName) {
    QStringList args = { "ReprocessedProfileSplitter2",
            "-in", reprocTimeSeriesFileName,
            "-outrlist", reprocFileListFileName,
            "-outflist", reprocFlagsFileListFileName,
            "-compress", "1",
            "-ilxml"
    };
    args += allXmlsFileName;
    return args;
}

QStringList LaiRetrievalHandler::GetFittedProfileReprocArgs(const QString &allLaiTimeSeriesFileName, const QString &allErrTimeSeriesFileName,
                                       const QString &allMsksTimeSeriesFileName, const QString &fittedTimeSeriesFileName, const QStringList &listProducts) {
    QStringList fittedProfileReprocArgs = { "ProfileReprocessing",
          "-lai", allLaiTimeSeriesFileName,
          "-err", allErrTimeSeriesFileName,
          "-msks", allMsksTimeSeriesFileName,
          "-opf", fittedTimeSeriesFileName,
          "-algo", "fit",
          "-ilxml"
    };
    fittedProfileReprocArgs += listProducts;
    return fittedProfileReprocArgs;
}

QStringList LaiRetrievalHandler::GetFittedProfileReprocSplitterArgs(const QString &fittedTimeSeriesFileName, const QString &fittedFileListFileName,
                                                                    const QString &fittedFlagsFileListFileName,
                                                                    const QStringList &allXmlsFileName) {
    QStringList args = { "ReprocessedProfileSplitter2",
                "-in", fittedTimeSeriesFileName,
                "-outrlist", fittedFileListFileName,
                "-outflist", fittedFlagsFileListFileName,
                "-compress", "1",
                "-ilxml"
    };
    args += allXmlsFileName;
    return args;
}

QStringList LaiRetrievalHandler::GetProductFormatterArgs(TaskToSubmit &productFormatterTask, EventProcessingContext &ctx, const JobSubmittedEvent &event,
                                    const QStringList &listProducts, const QList<LAIProductFormatterParams> &productParams) {

    const QJsonObject &parameters = QJsonDocument::fromJson(event.parametersJson.toUtf8()).object();
    std::map<QString, QString> configParameters = ctx.GetJobConfigurationParameters(event.jobId, "processor.l3b.lai.");

    //const auto &targetFolder = productFormatterTask.GetFilePath("");
    const auto &targetFolder = GetFinalProductFolder(ctx, event.jobId, event.siteId);
    const auto &outPropsPath = productFormatterTask.GetFilePath(PRODUC_FORMATTER_OUT_PROPS_FILE);
    const auto &executionInfosPath = productFormatterTask.GetFilePath("executionInfos.xml");

    WriteExecutionInfosFile(executionInfosPath, configParameters, listProducts);

    QStringList productFormatterArgs = { "ProductFormatter",
                            "-destroot", targetFolder,
                            "-fileclass", "SVT1",
                            "-level", "L3B",
                            "-baseline", "01.00",
                            "-processor", "vegetation",
                            "-gipp", executionInfosPath,
                            "-outprops", outPropsPath};
    productFormatterArgs += "-il";
    productFormatterArgs += listProducts;

    productFormatterArgs += "-processor.vegetation.laindvi";
    for(const LAIProductFormatterParams &params: productParams) {
        productFormatterArgs += params.tileId;
        productFormatterArgs += params.listNdvi;
    }

    productFormatterArgs += "-processor.vegetation.laimonodate";
    for(const LAIProductFormatterParams &params: productParams) {
        productFormatterArgs += params.tileId;
        productFormatterArgs += params.listLaiMonoDate;
    }

    productFormatterArgs += "-processor.vegetation.laimonodateerr";
    for(const LAIProductFormatterParams &params: productParams) {
        productFormatterArgs += params.tileId;
        productFormatterArgs += params.listLaiMonoDateErr;
    }

    productFormatterArgs += "-processor.vegetation.laimdateflgs";
    for(const LAIProductFormatterParams &params: productParams) {
        productFormatterArgs += params.tileId;
        productFormatterArgs += params.listLaiMonoDateFlgs;
    }

    if(parameters.contains("reproc")) {
        productFormatterArgs += "-processor.vegetation.filelaireproc";
        for(const LAIProductFormatterParams &params: productParams) {
            productFormatterArgs += params.tileId;
            productFormatterArgs += params.fileLaiReproc;
        }
        productFormatterArgs += "-processor.vegetation.filelaireprocflgs";
        for(const LAIProductFormatterParams &params: productParams) {
            productFormatterArgs += params.tileId;
            productFormatterArgs += params.fileLaiReprocFlgs;
        }
    }

    if(parameters.contains("fitted")) {
        productFormatterArgs += "-processor.vegetation.filelaifit";
        for(const LAIProductFormatterParams &params: productParams) {
            productFormatterArgs += params.tileId;
            productFormatterArgs += params.fileLaiFit;
        }
        productFormatterArgs += "-processor.vegetation.filelaifitflgs";
        for(const LAIProductFormatterParams &params: productParams) {
            productFormatterArgs += params.tileId;
            productFormatterArgs += params.fileLaiFitFlgs;
        }
    }
    return productFormatterArgs;
}

void LaiRetrievalHandler::GetStepsToGenModel(std::map<QString, QString> &configParameters,
                                             const QStringList &listProducts,
                                             QList<TaskToSubmit> &allTasksList,
                                             NewStepList &steps)
{
    const auto &modelsFolder = configParameters["processor.l3b.lai.modelsfolder"];
    const auto &rsrCfgFile = configParameters["processor.l3b.lai.rsrcfgfile"];
    int i = 0;
    int TasksNoPerProduct = LAI_TASKS_PER_PRODUCT + MODEL_GEN_TASKS_PER_PRODUCT;
    for(const QString& curXml : listProducts) {
        int loopFirstIdx = i*TasksNoPerProduct;
        TaskToSubmit &bvInputVariableGenerationTask = allTasksList[loopFirstIdx];
        TaskToSubmit &prosailSimulatorTask = allTasksList[loopFirstIdx+1];
        TaskToSubmit &trainingDataGeneratorTask = allTasksList[loopFirstIdx+2];
        TaskToSubmit &inverseModelLearningTask = allTasksList[loopFirstIdx+3];

        const auto & generatedSampleFile = bvInputVariableGenerationTask.GetFilePath("out_bv_dist_samples.txt");
        const auto & simuReflsFile = prosailSimulatorTask.GetFilePath("out_simu_refls.txt");
        const auto & anglesFile = prosailSimulatorTask.GetFilePath("out_angles.txt");
        const auto & trainingFile = trainingDataGeneratorTask.GetFilePath("out_training.txt");
        const auto & modelFile = inverseModelLearningTask.GetFilePath("out_model.txt");
        const auto & errEstModelFile = inverseModelLearningTask.GetFilePath("out_err_est_model.txt");


        QStringList BVInputVariableGenerationArgs = GetBVInputVariableGenerationArgs(configParameters, generatedSampleFile);
        QStringList ProSailSimulatorArgs = GetProSailSimulatorArgs(curXml, generatedSampleFile, rsrCfgFile, simuReflsFile, anglesFile, configParameters);
        QStringList TrainingDataGeneratorArgs = GetTrainingDataGeneratorArgs(curXml, generatedSampleFile, simuReflsFile, trainingFile);
        QStringList InverseModelLearningArgs = GetInverseModelLearningArgs(trainingFile, curXml, modelFile, errEstModelFile, modelsFolder, configParameters);

        steps.append(bvInputVariableGenerationTask.CreateStep("BVInputVariableGeneration", BVInputVariableGenerationArgs));
        steps.append(prosailSimulatorTask.CreateStep("ProSailSimulator", ProSailSimulatorArgs));
        steps.append(trainingDataGeneratorTask.CreateStep("TrainingDataGenerator", TrainingDataGeneratorArgs));
        steps.append(inverseModelLearningTask.CreateStep("InverseModelLearning", InverseModelLearningArgs));
        i++;
    }
}

QStringList LaiRetrievalHandler::GetBVInputVariableGenerationArgs(std::map<QString, QString> &configParameters, const QString &strGenSampleFile) {
    QString samplesNo = GetDefaultCfgVal(configParameters, "processor.l3b.lai.models.samples", DEFAULT_GENERATED_SAMPLES_NO);
    return { "BVInputVariableGeneration",
                "-samples", samplesNo,
                "-out", strGenSampleFile
    };
}

QStringList LaiRetrievalHandler::GetProSailSimulatorArgs(const QString &product, const QString &bvFileName, const QString &rsrCfgFileName,
                                                         const QString &outSimuReflsFile, const QString &outAngles, std::map<QString, QString> &configParameters) {
    QString noiseVar = GetDefaultCfgVal(configParameters, "processor.l3b.lai.models.noisevar", DEFAULT_NOISE_VAR);
    return { "ProSailSimulator",
                "-xml", product,
                "-bvfile", bvFileName,
                "-rsrcfg", rsrCfgFileName,
                "-out", outSimuReflsFile,
                "-outangles", outAngles,
                "-noisevar", noiseVar
    };
}

QStringList LaiRetrievalHandler::GetTrainingDataGeneratorArgs(const QString &product, const QString &biovarsFile,
                                                              const QString &simuReflsFile, const QString &outTrainingFile) {
    return { "TrainingDataGenerator",
                "-xml", product,
                "-biovarsfile", biovarsFile,
                "-simureflsfile", simuReflsFile,
                "-outtrainfile", outTrainingFile,
                "-addrefls", "1"
    };
}

QStringList LaiRetrievalHandler::GetInverseModelLearningArgs(const QString &trainingFile, const QString &product, const QString &modelFile,
                                                             const QString &errEstFile, const QString &modelsFolder,
                                                             std::map<QString, QString> &configParameters) {
    QString bestOf = GetDefaultCfgVal(configParameters, "processor.l3b.lai.models.bestof", DEFAULT_BEST_OF);
    QString regressor = GetDefaultCfgVal(configParameters, "processor.l3b.lai.models.regressor", DEFAULT_REGRESSOR);

    return { "InverseModelLearning",
                "-training", trainingFile,
                "-out", modelFile,
                "-errest", errEstFile,
                "-regression", regressor,
                "-bestof", bestOf,
                "-xml", product,
                "-newnamesoutfolder", modelsFolder
    };
}

const QString& LaiRetrievalHandler::GetDefaultCfgVal(std::map<QString, QString> &configParameters, const QString &key, const QString &defVal) {
    auto search = configParameters.find(key);
    if(search != configParameters.end()) {
        return search->second;
    }
    return defVal;
}

ProcessorJobDefinitionParams LaiRetrievalHandler::GetProcessingDefinitionImpl(SchedulingContext &ctx, int siteId, int scheduledDate,
                                                          const ConfigurationParameterValueMap &requestOverrideCfgValues)
{
    ConfigurationParameterValueMap mapCfg = ctx.GetConfigurationParameters(QString("processor.l3b."), siteId, requestOverrideCfgValues);

    ProcessorJobDefinitionParams params;
    params.isValid = false;

    //int generateModels = mapCfg["processor.l3b.generate_models"].value.toInt();
    int generateLai = mapCfg["processor.l3b.mono_date_lai"].value.toInt();
    int generateReprocess = mapCfg["processor.l3b.reprocess"].value.toInt();
    int generateFitted = mapCfg["processor.l3b.fitted"].value.toInt();
    int productionInterval = mapCfg["processor.l3b.production_interval"].value.toInt();

    QDateTime startDate;
    QDateTime endDate = QDateTime::fromTime_t(scheduledDate);
    if(generateLai || generateReprocess) {
        startDate = endDate.addDays(-productionInterval);
    }
    if(generateFitted) {
        QDateTime seasonStartDate;
        QDateTime seasonEndDate;
        GetSeasonStartEndDates(ctx, siteId, seasonStartDate, seasonEndDate, requestOverrideCfgValues);
        // set the start date at the end of start season
        startDate = seasonStartDate;
    }

    params.productList = ctx.GetProducts(siteId, (int)ProductType::L2AProductTypeId, startDate, endDate);
    if (params.productList.size() > 0) {
        if(generateFitted) {
            if(params.productList.size() > 4) {
                params.isValid = true;
            }
        } else if (generateReprocess > 2) {
            params.isValid = true;
        }
    }

    return params;
}
