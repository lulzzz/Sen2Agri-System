#!/usr/bin/env python
from __future__ import print_function

import os
import shutil
import glob
import argparse
import csv
from sys import argv
import datetime
import subprocess
import pipes
import time
import xml.etree.ElementTree as ET
from xml.dom import minidom

def runCmd(cmdArray):
    start = time.time()
    print(" ".join(map(pipes.quote, cmdArray)))
    res = subprocess.call(cmdArray)
    print("OTB app finished in: {}".format(datetime.timedelta(seconds=(time.time() - start))))
    if res != 0:
        print("OTB application error")
        exit(1)
    return res

def prettify(elem):
    """Return a pretty-printed XML string for the Element.
    """
    rough_string = ET.tostring(elem, 'utf-8')
    reparsed = minidom.parseString(rough_string)
    return reparsed.toprettyxml(indent="  ")


parser = argparse.ArgumentParser(description='Composite Python processor')

parser.add_argument(
    '--applocation', help='The path where the sen2agri is built', required=True)
parser.add_argument('--syntdate', help='L3A synthesis date', required=True)
parser.add_argument('--synthalf', help='Half synthesis', required=True)
parser.add_argument(
    '--input', help='The list of products xml descriptors', required=True, nargs='+')
parser.add_argument(
    '--res', help='The requested resolution in meters', required=True)
parser.add_argument('--outdir', help="Output directory", required=True)
parser.add_argument(
    '--bandsmap', help="Bands mapping file location", required=True)
parser.add_argument('--scatteringcoef',
                    help="Scattering coefficient file. This file is requested in S2 case ONLY", required=False)
parser.add_argument('--tileid',
                    help="Tile id", required=False)

USE_COMPRESSION=True
REMOVE_TEMP=False

args = parser.parse_args()

syntDate = args.syntdate
syntHalf = args.synthalf
resolution = args.res
bandsMap = args.bandsmap
appLocation = args.applocation
outDir = args.outdir

compositeLocation = appLocation + '/Composite'
print("Composite location:{}".format(compositeLocation))

productFormatterLocation = appLocation + '/MACCSMetadata/src'
print("Product formater location:{}".format(productFormatterLocation))

#defintion for filenames
weightOtbLibsRoot = compositeLocation + '/WeightCalculation'
outSpotMasks = outDir + '/spot_masks.tif'
outImgBands = outDir + '/res' + resolution + '.tif'
outImgBandsAll = outDir + '/res' + resolution + '_all.tif'
outCld = outDir + '/cld' + resolution + '.tif'
outWat = outDir + '/wat' + resolution + '.tif'
outSnow = outDir + '/snow' + resolution + '.tif'
outAot = outDir + '/aot' + resolution + '.tif'
outWeightAotFile = outDir + '/WeightAot#.tif'
outWeightCloudFile = outDir + '/WeightCloud#.tif'
outTotalWeightFile = outDir + '/WeightTotal#.tif'
outL3AFile = outDir + '/L3AResult#_' + resolution + 'M.tif'

WEIGHT_AOT_MIN="0.33"
WEIGHT_AOT_MAX="1"
AOT_MAX="0.8"

COARSE_RES="240"
SIGMA_SMALL_CLD="2"
SIGMA_LARGE_CLD="10"

WEIGHT_DATE_MIN="0.10"

outWeights = outDir + '/L3AResult#_weights.tif'
outDates = outDir + '/L3AResult#_dates.tif'
outRefls = outDir + '/L3AResult#_refls.tif'
outFlags = outDir + '/L3AResult#_flags.tif'
outRGB = outDir + '/L3AResult#_rgb.tif'

fullScatCoeffs=""
tileID="TILE_none"

if os.path.exists(outDir):
    if not os.path.isdir(outDir):
        print("Can't create the output directory because there is a file with the same name")
        print("Remove: " + outDir)
        exit(1)
else:
    os.makedirs(outDir)

shutil.copyfile(bandsMap, os.path.join(outDir, os.path.basename(bandsMap)))

if args.scatteringcoef:
    shutil.copyfile(args.scatteringcoef, os.path.join(outDir, os.path.basename(args.scatteringcoef)))
    scatteringCoefFilename = args.scatteringcoef[args.scatteringcoef.rfind('/'):]
    fullScatCoeffs = outDir + scatteringCoefFilename
    print(fullScatCoeffs)

if args.tileid:
    tileID = "TILE_{}".format(args.tileid)

paramsFilenameXML= outDir + '/params.xml'
with open(paramsFilenameXML, 'w') as paramsFileXML:
    root = ET.Element('metadata')
    wAOT = ET.SubElement(root, "Weight_AOT")
    ET.SubElement(wAOT, "weight_aot_min").text = WEIGHT_AOT_MIN
    ET.SubElement(wAOT, "weight_aot_max").text = WEIGHT_AOT_MAX
    ET.SubElement(wAOT, "aot_max").text = AOT_MAX
    wCLD = ET.SubElement(root, "Weight_On_Clouds")
    ET.SubElement(wCLD, "coarse_res").text = COARSE_RES
    ET.SubElement(wCLD, "sigma_small_cloud").text = SIGMA_SMALL_CLD
    ET.SubElement(wCLD, "sigma_large_cloud").text = SIGMA_LARGE_CLD
    wDATE = ET.SubElement(root, "Weight_On_Date")
    ET.SubElement(wDATE, "weight_date_min").text = WEIGHT_DATE_MIN
    ET.SubElement(wDATE, "l3a_product_date").text = syntDate
    ET.SubElement(wDATE, "half_synthesis").text = syntHalf
    dates = ET.SubElement(root, "Dates_information")
    ET.SubElement(dates, "synthesis_date").text = syntDate
    ET.SubElement(dates, "synthesis_half").text = syntHalf
    usedXMLs = ET.SubElement(root, "XML_files")
    i = 0
    for xml in args.input:
        ET.SubElement(usedXMLs, "XML_" + str(i)).text = xml
        i += 1
    paramsFileXML.write(prettify(root))

paramsFilename= outDir + '/params.txt'
with open(paramsFilename, 'w') as paramsFile:
    paramsFile.write("Weight AOT\n")
    paramsFile.write("    weight aot min    = " + WEIGHT_AOT_MIN + "\n")
    paramsFile.write("    weight aot max    = " + WEIGHT_AOT_MAX + "\n")
    paramsFile.write("    aot max           = " + AOT_MAX + "\n")
    paramsFile.write("Weight on clouds\n")
    paramsFile.write("    coarse res        = " + COARSE_RES + "\n")
    paramsFile.write("    sigma small cloud = " + SIGMA_SMALL_CLD + "\n")
    paramsFile.write("    sigma large cloud = " + SIGMA_LARGE_CLD + "\n")
    paramsFile.write("Weight on Date\n")
    paramsFile.write("    weight date min   = " + WEIGHT_DATE_MIN + "\n")
    paramsFile.write("    l3a product date  = " + syntDate + "\n")
    paramsFile.write("    half synthesis    = " + syntHalf + "\n")
    paramsFile.write("Dates information\n")
    paramsFile.write("    synthesis date    = " + syntDate + "\n")
    paramsFile.write("    synthesis half    = " + syntHalf + "\n")
    paramsFile.write(" ")
    paramsFile.write("Used XML files\n")
    for xml in args.input:
        paramsFile.write("  " + xml + "\n")

prevL3A=[]
i = 0

print("Processing started: " + str(datetime.datetime.now()))
start = time.time()
for xml in args.input:

    runCmd(["otbcli", "MaskHandler", compositeLocation + '/MaskHandler', "-xml", xml, "-out", outSpotMasks, "-sentinelres", resolution])

    counterString = str(i)
    mod=outL3AFile.replace("#", counterString)
    out_w=outWeights.replace("#", counterString)
    out_d=outDates.replace("#", counterString)
    out_r=outRefls.replace("#", counterString)
    out_f=outFlags.replace("#", counterString)
    out_rgb=outRGB.replace("#", counterString)

    out_w_Aot=outWeightAotFile.replace("#", counterString)
    out_w_Cloud=outWeightCloudFile.replace("#", counterString)
    out_w_Total=outTotalWeightFile.replace("#", counterString)

    if fullScatCoeffs:
        cmd = ["otbcli", "CompositePreprocessing2", compositeLocation + '/CompositePreprocessing', "-xml", xml, "-bmap", bandsMap, "-res", resolution, "-scatcoef", fullScatCoeffs, "-msk", outSpotMasks, "-outres", outImgBands, "-outcmres", outCld, "-outwmres", outWat, "-outsmres", outSnow, "-outaotres", outAot]
    else:
        cmd = ["otbcli", "CompositePreprocessing2", compositeLocation + '/CompositePreprocessing', "-xml", xml, "-bmap", bandsMap, "-res", resolution, "-msk", outSpotMasks, "-outres", outImgBands, "-outcmres", outCld, "-outwmres", outWat, "-outsmres", outSnow, "-outaotres", outAot]

    runCmd(cmd)

    runCmd(["otbcli", "WeightAOT", weightOtbLibsRoot + '/WeightAOT', "-xml", xml, "-in", outAot, "-waotmin", WEIGHT_AOT_MIN, "-waotmax", WEIGHT_AOT_MAX, "-aotmax", AOT_MAX, "-out", out_w_Aot])

    runCmd(["otbcli", "WeightOnClouds", weightOtbLibsRoot + '/WeightOnClouds', "-inxml", xml, "-incldmsk", outCld, "-coarseres", COARSE_RES, "-sigmasmallcld", SIGMA_SMALL_CLD, "-sigmalargecld", SIGMA_LARGE_CLD, "-out", out_w_Cloud])

    runCmd(["otbcli", "TotalWeight", weightOtbLibsRoot + '/TotalWeight', "-xml", xml, "-waotfile", out_w_Aot, "-wcldfile", out_w_Cloud, "-l3adate", syntDate, "-halfsynthesis", syntHalf, "-wdatemin", WEIGHT_DATE_MIN, "-out", out_w_Total])
    #todo... search for previous L3A produc?

    runCmd(["otbcli", "UpdateSynthesis", compositeLocation + '/UpdateSynthesis', "-in", outImgBands, "-bmap", bandsMap, "-xml", xml, "-csm", outCld, "-wm", outWat, "-sm", outSnow, "-wl2a", out_w_Total, "-out", mod] + prevL3A)

    tmpOut_w = out_w
    tmpOut_d = out_d
    tmpOut_r = out_r
    tmpOut_f = out_f
    tmpOut_rgb = out_rgb

    if USE_COMPRESSION:
        tmpOut_w += '?gdal:co:COMPRESS=DEFLATE'
        tmpOut_d += '?gdal:co:COMPRESS=DEFLATE'
        tmpOut_r += '?gdal:co:COMPRESS=DEFLATE'
        tmpOut_f += '?gdal:co:COMPRESS=DEFLATE'
        tmpOut_rgb += '?gdal:co:COMPRESS=DEFLATE'

    runCmd(["otbcli", "CompositeSplitter2", compositeLocation + '/CompositeSplitter', "-in", mod, "-xml", xml, "-bmap", bandsMap, "-outweights", tmpOut_w, "-outdates", tmpOut_d, "-outrefls", tmpOut_r, "-outflags", tmpOut_f, "-outrgb", tmpOut_rgb])

    prevL3A = ["-prevl3aw", out_w, "-prevl3ad", out_d, "-prevl3ar", out_r, "-prevl3af", out_f]

    if REMOVE_TEMP:
        try:
            os.remove(outWeightAotFile.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outWeightCloudFile.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outTotalWeightFile.replace("#", counterString))
        except:
            pass

    runCmd(["rm", "-fr", mod, outSpotMasks, outImgBands, outCld, outWat, outSnow, outAot])
    if REMOVE_TEMP and i > 0:
        counterString = str(i - 1)
        print("The following files will be deleted:")
        print(outWeights.replace("#", counterString))
        print(outDates.replace("#", counterString))
        print(outRefls.replace("#", counterString))
        print(outFlags.replace("#", counterString))
        print(outRGB.replace("#", counterString))
        try:
            os.remove(outWeights.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outDates.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outRefls.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outFlags.replace("#", counterString))
        except:
            pass
        try:
            os.remove(outRGB.replace("#", counterString))
        except:
            pass
    i += 1
if i == 0:
    print("No L2A products found !")
    exit(1)

i -= 1
runCmd(["otbcli", "ProductFormatter", productFormatterLocation, "-destroot", outDir, "-fileclass", "SVT1", "-level", "L3A", "-timeperiod", syntDate, "-baseline", "01.00", "-processor", "composite", "-processor.composite.refls", tileID, out_r, "-processor.composite.weights", tileID, out_w, "-processor.composite.flags", tileID, out_f, "-processor.composite.dates", tileID, out_d, "-processor.composite.rgb", tileID, out_rgb, "-il", args.input[-1], "-gipp", paramsFilenameXML])

if REMOVE_TEMP:
    counterString = str(i)
    print("The following files will be deleted:")
    print(outWeights.replace("#", counterString))
    print(outDates.replace("#", counterString))
    print(outRefls.replace("#", counterString))
    print(outFlags.replace("#", counterString))
    try:
        os.remove(outWeights.replace("#", counterString))
    except:
        pass
    try:
        os.remove(outDates.replace("#", counterString))
    except:
        pass
    try:
        os.remove(outRefls.replace("#", counterString))
    except:
        pass
    try:
        os.remove(outFlags.replace("#", counterString))
    except:
        pass
#    try:
#        os.remove(outRGB.replace("#", counterString))
#    except:
#        pass


print("Processing finished: " + str(datetime.datetime.now()))
print("Total execution time: {}".format(datetime.timedelta(seconds=(time.time() - start))))

'''
''' and None
