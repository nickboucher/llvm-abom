; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -passes=lower-matrix-intrinsics,instcombine -fuse-matrix-tile-size=2 -matrix-allow-contract -force-fuse-matrix -verify-dom-info %s -S | FileCheck %s

; REQUIRES: aarch64-registered-target

target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "aarch64-apple-ios"

define void @test(ptr %A, ptr %B, ptr %C, i1 %cond) {
; CHECK-LABEL: @test(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[COL_LOAD133:%.*]] = load <3 x double>, ptr [[A:%.*]], align 8
; CHECK-NEXT:    [[VEC_GEP134:%.*]] = getelementptr i8, ptr [[A]], i64 24
; CHECK-NEXT:    [[COL_LOAD135:%.*]] = load <3 x double>, ptr [[VEC_GEP134]], align 8
; CHECK-NEXT:    [[COL_LOAD136:%.*]] = load <2 x double>, ptr [[B:%.*]], align 8
; CHECK-NEXT:    [[VEC_GEP137:%.*]] = getelementptr i8, ptr [[B]], i64 16
; CHECK-NEXT:    [[COL_LOAD138:%.*]] = load <2 x double>, ptr [[VEC_GEP137]], align 8
; CHECK-NEXT:    [[VEC_GEP139:%.*]] = getelementptr i8, ptr [[B]], i64 32
; CHECK-NEXT:    [[COL_LOAD140:%.*]] = load <2 x double>, ptr [[VEC_GEP139]], align 8
; CHECK-NEXT:    [[STORE_BEGIN:%.*]] = ptrtoint ptr [[C:%.*]] to i64
; CHECK-NEXT:    [[STORE_END:%.*]] = add nuw nsw i64 [[STORE_BEGIN]], 72
; CHECK-NEXT:    [[LOAD_BEGIN:%.*]] = ptrtoint ptr [[A]] to i64
; CHECK-NEXT:    [[TMP0:%.*]] = icmp ugt i64 [[STORE_END]], [[LOAD_BEGIN]]
; CHECK-NEXT:    br i1 [[TMP0]], label [[ALIAS_CONT:%.*]], label [[NO_ALIAS:%.*]]
; CHECK:       alias_cont:
; CHECK-NEXT:    [[LOAD_END:%.*]] = add nuw nsw i64 [[LOAD_BEGIN]], 48
; CHECK-NEXT:    [[TMP1:%.*]] = icmp ugt i64 [[LOAD_END]], [[STORE_BEGIN]]
; CHECK-NEXT:    br i1 [[TMP1]], label [[COPY:%.*]], label [[NO_ALIAS]]
; CHECK:       copy:
; CHECK-NEXT:    [[TMP2:%.*]] = alloca [6 x double], align 8
; CHECK-NEXT:    call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(48) [[TMP2]], ptr noundef nonnull align 8 dereferenceable(48) [[A]], i64 48, i1 false)
; CHECK-NEXT:    br label [[NO_ALIAS]]
; CHECK:       no_alias:
; CHECK-NEXT:    [[TMP3:%.*]] = phi ptr [ [[A]], [[ENTRY:%.*]] ], [ [[A]], [[ALIAS_CONT]] ], [ [[TMP2]], [[COPY]] ]
; CHECK-NEXT:    [[STORE_BEGIN4:%.*]] = ptrtoint ptr [[C]] to i64
; CHECK-NEXT:    [[STORE_END5:%.*]] = add nuw nsw i64 [[STORE_BEGIN4]], 72
; CHECK-NEXT:    [[LOAD_BEGIN6:%.*]] = ptrtoint ptr [[B]] to i64
; CHECK-NEXT:    [[TMP4:%.*]] = icmp ugt i64 [[STORE_END5]], [[LOAD_BEGIN6]]
; CHECK-NEXT:    br i1 [[TMP4]], label [[ALIAS_CONT1:%.*]], label [[NO_ALIAS3:%.*]]
; CHECK:       alias_cont1:
; CHECK-NEXT:    [[LOAD_END7:%.*]] = add nuw nsw i64 [[LOAD_BEGIN6]], 48
; CHECK-NEXT:    [[TMP5:%.*]] = icmp ugt i64 [[LOAD_END7]], [[STORE_BEGIN4]]
; CHECK-NEXT:    br i1 [[TMP5]], label [[COPY2:%.*]], label [[NO_ALIAS3]]
; CHECK:       copy2:
; CHECK-NEXT:    [[TMP6:%.*]] = alloca [6 x double], align 8
; CHECK-NEXT:    call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(48) [[TMP6]], ptr noundef nonnull align 8 dereferenceable(48) [[B]], i64 48, i1 false)
; CHECK-NEXT:    br label [[NO_ALIAS3]]
; CHECK:       no_alias3:
; CHECK-NEXT:    [[TMP7:%.*]] = phi ptr [ [[B]], [[NO_ALIAS]] ], [ [[B]], [[ALIAS_CONT1]] ], [ [[TMP6]], [[COPY2]] ]
; CHECK-NEXT:    [[COL_LOAD:%.*]] = load <2 x double>, ptr [[TMP3]], align 8
; CHECK-NEXT:    [[VEC_GEP:%.*]] = getelementptr i8, ptr [[TMP3]], i64 24
; CHECK-NEXT:    [[COL_LOAD8:%.*]] = load <2 x double>, ptr [[VEC_GEP]], align 8
; CHECK-NEXT:    [[COL_LOAD9:%.*]] = load <2 x double>, ptr [[TMP7]], align 8
; CHECK-NEXT:    [[VEC_GEP10:%.*]] = getelementptr i8, ptr [[TMP7]], i64 16
; CHECK-NEXT:    [[COL_LOAD11:%.*]] = load <2 x double>, ptr [[VEC_GEP10]], align 8
; CHECK-NEXT:    [[SPLAT_SPLAT:%.*]] = shufflevector <2 x double> [[COL_LOAD9]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP8:%.*]] = fmul contract <2 x double> [[COL_LOAD]], [[SPLAT_SPLAT]]
; CHECK-NEXT:    [[SPLAT_SPLAT14:%.*]] = shufflevector <2 x double> [[COL_LOAD9]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP9:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD8]], <2 x double> [[SPLAT_SPLAT14]], <2 x double> [[TMP8]])
; CHECK-NEXT:    [[SPLAT_SPLAT17:%.*]] = shufflevector <2 x double> [[COL_LOAD11]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP10:%.*]] = fmul contract <2 x double> [[COL_LOAD]], [[SPLAT_SPLAT17]]
; CHECK-NEXT:    [[SPLAT_SPLAT20:%.*]] = shufflevector <2 x double> [[COL_LOAD11]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP11:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD8]], <2 x double> [[SPLAT_SPLAT20]], <2 x double> [[TMP10]])
; CHECK-NEXT:    store <2 x double> [[TMP9]], ptr [[C]], align 8
; CHECK-NEXT:    [[VEC_GEP21:%.*]] = getelementptr i8, ptr [[C]], i64 24
; CHECK-NEXT:    store <2 x double> [[TMP11]], ptr [[VEC_GEP21]], align 8
; CHECK-NEXT:    [[TMP12:%.*]] = getelementptr i8, ptr [[TMP3]], i64 16
; CHECK-NEXT:    [[COL_LOAD22:%.*]] = load <1 x double>, ptr [[TMP12]], align 8
; CHECK-NEXT:    [[VEC_GEP23:%.*]] = getelementptr i8, ptr [[TMP3]], i64 40
; CHECK-NEXT:    [[COL_LOAD24:%.*]] = load <1 x double>, ptr [[VEC_GEP23]], align 8
; CHECK-NEXT:    [[COL_LOAD25:%.*]] = load <2 x double>, ptr [[TMP7]], align 8
; CHECK-NEXT:    [[VEC_GEP26:%.*]] = getelementptr i8, ptr [[TMP7]], i64 16
; CHECK-NEXT:    [[COL_LOAD27:%.*]] = load <2 x double>, ptr [[VEC_GEP26]], align 8
; CHECK-NEXT:    [[SPLAT_SPLATINSERT29:%.*]] = shufflevector <2 x double> [[COL_LOAD25]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP13:%.*]] = fmul contract <1 x double> [[COL_LOAD22]], [[SPLAT_SPLATINSERT29]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT32:%.*]] = shufflevector <2 x double> [[COL_LOAD25]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP14:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD24]], <1 x double> [[SPLAT_SPLATINSERT32]], <1 x double> [[TMP13]])
; CHECK-NEXT:    [[SPLAT_SPLATINSERT35:%.*]] = shufflevector <2 x double> [[COL_LOAD27]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP15:%.*]] = fmul contract <1 x double> [[COL_LOAD22]], [[SPLAT_SPLATINSERT35]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT38:%.*]] = shufflevector <2 x double> [[COL_LOAD27]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP16:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD24]], <1 x double> [[SPLAT_SPLATINSERT38]], <1 x double> [[TMP15]])
; CHECK-NEXT:    [[TMP17:%.*]] = getelementptr i8, ptr [[C]], i64 16
; CHECK-NEXT:    store <1 x double> [[TMP14]], ptr [[TMP17]], align 8
; CHECK-NEXT:    [[VEC_GEP40:%.*]] = getelementptr i8, ptr [[C]], i64 40
; CHECK-NEXT:    store <1 x double> [[TMP16]], ptr [[VEC_GEP40]], align 8
; CHECK-NEXT:    [[COL_LOAD41:%.*]] = load <2 x double>, ptr [[TMP3]], align 8
; CHECK-NEXT:    [[VEC_GEP42:%.*]] = getelementptr i8, ptr [[TMP3]], i64 24
; CHECK-NEXT:    [[COL_LOAD43:%.*]] = load <2 x double>, ptr [[VEC_GEP42]], align 8
; CHECK-NEXT:    [[TMP18:%.*]] = getelementptr i8, ptr [[TMP7]], i64 32
; CHECK-NEXT:    [[COL_LOAD44:%.*]] = load <2 x double>, ptr [[TMP18]], align 8
; CHECK-NEXT:    [[SPLAT_SPLAT47:%.*]] = shufflevector <2 x double> [[COL_LOAD44]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP19:%.*]] = fmul contract <2 x double> [[COL_LOAD41]], [[SPLAT_SPLAT47]]
; CHECK-NEXT:    [[SPLAT_SPLAT50:%.*]] = shufflevector <2 x double> [[COL_LOAD44]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP20:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD43]], <2 x double> [[SPLAT_SPLAT50]], <2 x double> [[TMP19]])
; CHECK-NEXT:    [[TMP21:%.*]] = getelementptr i8, ptr [[C]], i64 48
; CHECK-NEXT:    store <2 x double> [[TMP20]], ptr [[TMP21]], align 8
; CHECK-NEXT:    [[TMP22:%.*]] = getelementptr i8, ptr [[TMP3]], i64 16
; CHECK-NEXT:    [[COL_LOAD51:%.*]] = load <1 x double>, ptr [[TMP22]], align 8
; CHECK-NEXT:    [[VEC_GEP52:%.*]] = getelementptr i8, ptr [[TMP3]], i64 40
; CHECK-NEXT:    [[COL_LOAD53:%.*]] = load <1 x double>, ptr [[VEC_GEP52]], align 8
; CHECK-NEXT:    [[TMP23:%.*]] = getelementptr i8, ptr [[TMP7]], i64 32
; CHECK-NEXT:    [[COL_LOAD54:%.*]] = load <2 x double>, ptr [[TMP23]], align 8
; CHECK-NEXT:    [[SPLAT_SPLATINSERT56:%.*]] = shufflevector <2 x double> [[COL_LOAD54]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP24:%.*]] = fmul contract <1 x double> [[COL_LOAD51]], [[SPLAT_SPLATINSERT56]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT59:%.*]] = shufflevector <2 x double> [[COL_LOAD54]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP25:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD53]], <1 x double> [[SPLAT_SPLATINSERT59]], <1 x double> [[TMP24]])
; CHECK-NEXT:    [[TMP26:%.*]] = getelementptr i8, ptr [[C]], i64 64
; CHECK-NEXT:    store <1 x double> [[TMP25]], ptr [[TMP26]], align 8
; CHECK-NEXT:    br i1 [[COND:%.*]], label [[TRUE:%.*]], label [[FALSE:%.*]]
; CHECK:       true:
; CHECK-NEXT:    [[TMP27:%.*]] = fadd contract <3 x double> [[COL_LOAD133]], [[COL_LOAD133]]
; CHECK-NEXT:    [[TMP28:%.*]] = fadd contract <3 x double> [[COL_LOAD135]], [[COL_LOAD135]]
; CHECK-NEXT:    store <3 x double> [[TMP27]], ptr [[A]], align 8
; CHECK-NEXT:    [[VEC_GEP143:%.*]] = getelementptr i8, ptr [[A]], i64 24
; CHECK-NEXT:    store <3 x double> [[TMP28]], ptr [[VEC_GEP143]], align 8
; CHECK-NEXT:    br label [[END:%.*]]
; CHECK:       false:
; CHECK-NEXT:    [[TMP29:%.*]] = fadd contract <2 x double> [[COL_LOAD136]], [[COL_LOAD136]]
; CHECK-NEXT:    [[TMP30:%.*]] = fadd contract <2 x double> [[COL_LOAD138]], [[COL_LOAD138]]
; CHECK-NEXT:    [[TMP31:%.*]] = fadd contract <2 x double> [[COL_LOAD140]], [[COL_LOAD140]]
; CHECK-NEXT:    store <2 x double> [[TMP29]], ptr [[B]], align 8
; CHECK-NEXT:    [[VEC_GEP141:%.*]] = getelementptr i8, ptr [[B]], i64 16
; CHECK-NEXT:    store <2 x double> [[TMP30]], ptr [[VEC_GEP141]], align 8
; CHECK-NEXT:    [[VEC_GEP142:%.*]] = getelementptr i8, ptr [[B]], i64 32
; CHECK-NEXT:    store <2 x double> [[TMP31]], ptr [[VEC_GEP142]], align 8
; CHECK-NEXT:    br label [[END]]
; CHECK:       end:
; CHECK-NEXT:    [[STORE_BEGIN64:%.*]] = ptrtoint ptr [[C]] to i64
; CHECK-NEXT:    [[STORE_END65:%.*]] = add nuw nsw i64 [[STORE_BEGIN64]], 72
; CHECK-NEXT:    [[LOAD_BEGIN66:%.*]] = ptrtoint ptr [[A]] to i64
; CHECK-NEXT:    [[TMP32:%.*]] = icmp ugt i64 [[STORE_END65]], [[LOAD_BEGIN66]]
; CHECK-NEXT:    br i1 [[TMP32]], label [[ALIAS_CONT61:%.*]], label [[NO_ALIAS63:%.*]]
; CHECK:       alias_cont61:
; CHECK-NEXT:    [[LOAD_END67:%.*]] = add nuw nsw i64 [[LOAD_BEGIN66]], 48
; CHECK-NEXT:    [[TMP33:%.*]] = icmp ugt i64 [[LOAD_END67]], [[STORE_BEGIN64]]
; CHECK-NEXT:    br i1 [[TMP33]], label [[COPY62:%.*]], label [[NO_ALIAS63]]
; CHECK:       copy62:
; CHECK-NEXT:    [[TMP34:%.*]] = alloca [6 x double], align 8
; CHECK-NEXT:    call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(48) [[TMP34]], ptr noundef nonnull align 8 dereferenceable(48) [[A]], i64 48, i1 false)
; CHECK-NEXT:    br label [[NO_ALIAS63]]
; CHECK:       no_alias63:
; CHECK-NEXT:    [[TMP35:%.*]] = phi ptr [ [[A]], [[END]] ], [ [[A]], [[ALIAS_CONT61]] ], [ [[TMP34]], [[COPY62]] ]
; CHECK-NEXT:    [[STORE_BEGIN71:%.*]] = ptrtoint ptr [[C]] to i64
; CHECK-NEXT:    [[STORE_END72:%.*]] = add nuw nsw i64 [[STORE_BEGIN71]], 72
; CHECK-NEXT:    [[LOAD_BEGIN73:%.*]] = ptrtoint ptr [[B]] to i64
; CHECK-NEXT:    [[TMP36:%.*]] = icmp ugt i64 [[STORE_END72]], [[LOAD_BEGIN73]]
; CHECK-NEXT:    br i1 [[TMP36]], label [[ALIAS_CONT68:%.*]], label [[NO_ALIAS70:%.*]]
; CHECK:       alias_cont68:
; CHECK-NEXT:    [[LOAD_END74:%.*]] = add nuw nsw i64 [[LOAD_BEGIN73]], 48
; CHECK-NEXT:    [[TMP37:%.*]] = icmp ugt i64 [[LOAD_END74]], [[STORE_BEGIN71]]
; CHECK-NEXT:    br i1 [[TMP37]], label [[COPY69:%.*]], label [[NO_ALIAS70]]
; CHECK:       copy69:
; CHECK-NEXT:    [[TMP38:%.*]] = alloca [6 x double], align 8
; CHECK-NEXT:    call void @llvm.memcpy.p0.p0.i64(ptr noundef nonnull align 8 dereferenceable(48) [[TMP38]], ptr noundef nonnull align 8 dereferenceable(48) [[B]], i64 48, i1 false)
; CHECK-NEXT:    br label [[NO_ALIAS70]]
; CHECK:       no_alias70:
; CHECK-NEXT:    [[TMP39:%.*]] = phi ptr [ [[B]], [[NO_ALIAS63]] ], [ [[B]], [[ALIAS_CONT68]] ], [ [[TMP38]], [[COPY69]] ]
; CHECK-NEXT:    [[COL_LOAD75:%.*]] = load <2 x double>, ptr [[TMP35]], align 8
; CHECK-NEXT:    [[VEC_GEP76:%.*]] = getelementptr i8, ptr [[TMP35]], i64 24
; CHECK-NEXT:    [[COL_LOAD77:%.*]] = load <2 x double>, ptr [[VEC_GEP76]], align 8
; CHECK-NEXT:    [[COL_LOAD78:%.*]] = load <2 x double>, ptr [[TMP39]], align 8
; CHECK-NEXT:    [[VEC_GEP79:%.*]] = getelementptr i8, ptr [[TMP39]], i64 16
; CHECK-NEXT:    [[COL_LOAD80:%.*]] = load <2 x double>, ptr [[VEC_GEP79]], align 8
; CHECK-NEXT:    [[SPLAT_SPLAT83:%.*]] = shufflevector <2 x double> [[COL_LOAD78]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP40:%.*]] = fmul contract <2 x double> [[COL_LOAD75]], [[SPLAT_SPLAT83]]
; CHECK-NEXT:    [[SPLAT_SPLAT86:%.*]] = shufflevector <2 x double> [[COL_LOAD78]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP41:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD77]], <2 x double> [[SPLAT_SPLAT86]], <2 x double> [[TMP40]])
; CHECK-NEXT:    [[SPLAT_SPLAT89:%.*]] = shufflevector <2 x double> [[COL_LOAD80]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP42:%.*]] = fmul contract <2 x double> [[COL_LOAD75]], [[SPLAT_SPLAT89]]
; CHECK-NEXT:    [[SPLAT_SPLAT92:%.*]] = shufflevector <2 x double> [[COL_LOAD80]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP43:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD77]], <2 x double> [[SPLAT_SPLAT92]], <2 x double> [[TMP42]])
; CHECK-NEXT:    store <2 x double> [[TMP41]], ptr [[C]], align 8
; CHECK-NEXT:    [[VEC_GEP93:%.*]] = getelementptr i8, ptr [[C]], i64 24
; CHECK-NEXT:    store <2 x double> [[TMP43]], ptr [[VEC_GEP93]], align 8
; CHECK-NEXT:    [[TMP44:%.*]] = getelementptr i8, ptr [[TMP35]], i64 16
; CHECK-NEXT:    [[COL_LOAD94:%.*]] = load <1 x double>, ptr [[TMP44]], align 8
; CHECK-NEXT:    [[VEC_GEP95:%.*]] = getelementptr i8, ptr [[TMP35]], i64 40
; CHECK-NEXT:    [[COL_LOAD96:%.*]] = load <1 x double>, ptr [[VEC_GEP95]], align 8
; CHECK-NEXT:    [[COL_LOAD97:%.*]] = load <2 x double>, ptr [[TMP39]], align 8
; CHECK-NEXT:    [[VEC_GEP98:%.*]] = getelementptr i8, ptr [[TMP39]], i64 16
; CHECK-NEXT:    [[COL_LOAD99:%.*]] = load <2 x double>, ptr [[VEC_GEP98]], align 8
; CHECK-NEXT:    [[SPLAT_SPLATINSERT101:%.*]] = shufflevector <2 x double> [[COL_LOAD97]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP45:%.*]] = fmul contract <1 x double> [[COL_LOAD94]], [[SPLAT_SPLATINSERT101]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT104:%.*]] = shufflevector <2 x double> [[COL_LOAD97]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP46:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD96]], <1 x double> [[SPLAT_SPLATINSERT104]], <1 x double> [[TMP45]])
; CHECK-NEXT:    [[SPLAT_SPLATINSERT107:%.*]] = shufflevector <2 x double> [[COL_LOAD99]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP47:%.*]] = fmul contract <1 x double> [[COL_LOAD94]], [[SPLAT_SPLATINSERT107]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT110:%.*]] = shufflevector <2 x double> [[COL_LOAD99]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP48:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD96]], <1 x double> [[SPLAT_SPLATINSERT110]], <1 x double> [[TMP47]])
; CHECK-NEXT:    [[TMP49:%.*]] = getelementptr i8, ptr [[C]], i64 16
; CHECK-NEXT:    store <1 x double> [[TMP46]], ptr [[TMP49]], align 8
; CHECK-NEXT:    [[VEC_GEP112:%.*]] = getelementptr i8, ptr [[C]], i64 40
; CHECK-NEXT:    store <1 x double> [[TMP48]], ptr [[VEC_GEP112]], align 8
; CHECK-NEXT:    [[COL_LOAD113:%.*]] = load <2 x double>, ptr [[TMP35]], align 8
; CHECK-NEXT:    [[VEC_GEP114:%.*]] = getelementptr i8, ptr [[TMP35]], i64 24
; CHECK-NEXT:    [[COL_LOAD115:%.*]] = load <2 x double>, ptr [[VEC_GEP114]], align 8
; CHECK-NEXT:    [[TMP50:%.*]] = getelementptr i8, ptr [[TMP39]], i64 32
; CHECK-NEXT:    [[COL_LOAD116:%.*]] = load <2 x double>, ptr [[TMP50]], align 8
; CHECK-NEXT:    [[SPLAT_SPLAT119:%.*]] = shufflevector <2 x double> [[COL_LOAD116]], <2 x double> poison, <2 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP51:%.*]] = fmul contract <2 x double> [[COL_LOAD113]], [[SPLAT_SPLAT119]]
; CHECK-NEXT:    [[SPLAT_SPLAT122:%.*]] = shufflevector <2 x double> [[COL_LOAD116]], <2 x double> poison, <2 x i32> <i32 1, i32 1>
; CHECK-NEXT:    [[TMP52:%.*]] = call contract <2 x double> @llvm.fmuladd.v2f64(<2 x double> [[COL_LOAD115]], <2 x double> [[SPLAT_SPLAT122]], <2 x double> [[TMP51]])
; CHECK-NEXT:    [[TMP53:%.*]] = getelementptr i8, ptr [[C]], i64 48
; CHECK-NEXT:    store <2 x double> [[TMP52]], ptr [[TMP53]], align 8
; CHECK-NEXT:    [[TMP54:%.*]] = getelementptr i8, ptr [[TMP35]], i64 16
; CHECK-NEXT:    [[COL_LOAD123:%.*]] = load <1 x double>, ptr [[TMP54]], align 8
; CHECK-NEXT:    [[VEC_GEP124:%.*]] = getelementptr i8, ptr [[TMP35]], i64 40
; CHECK-NEXT:    [[COL_LOAD125:%.*]] = load <1 x double>, ptr [[VEC_GEP124]], align 8
; CHECK-NEXT:    [[TMP55:%.*]] = getelementptr i8, ptr [[TMP39]], i64 32
; CHECK-NEXT:    [[COL_LOAD126:%.*]] = load <2 x double>, ptr [[TMP55]], align 8
; CHECK-NEXT:    [[SPLAT_SPLATINSERT128:%.*]] = shufflevector <2 x double> [[COL_LOAD126]], <2 x double> poison, <1 x i32> zeroinitializer
; CHECK-NEXT:    [[TMP56:%.*]] = fmul contract <1 x double> [[COL_LOAD123]], [[SPLAT_SPLATINSERT128]]
; CHECK-NEXT:    [[SPLAT_SPLATINSERT131:%.*]] = shufflevector <2 x double> [[COL_LOAD126]], <2 x double> poison, <1 x i32> <i32 1>
; CHECK-NEXT:    [[TMP57:%.*]] = call contract <1 x double> @llvm.fmuladd.v1f64(<1 x double> [[COL_LOAD125]], <1 x double> [[SPLAT_SPLATINSERT131]], <1 x double> [[TMP56]])
; CHECK-NEXT:    [[TMP58:%.*]] = getelementptr i8, ptr [[C]], i64 64
; CHECK-NEXT:    store <1 x double> [[TMP57]], ptr [[TMP58]], align 8
; CHECK-NEXT:    ret void
;
entry:
  %a = load <6 x double>, ptr %A, align 8
  %b = load <6 x double>, ptr %B, align 8
  %c = call <9 x double> @llvm.matrix.multiply(<6 x double> %a, <6 x double> %b, i32 3, i32 2, i32 3)
  store <9 x double> %c, ptr %C, align 8

  br i1 %cond, label %true, label %false

true:
  %a.add = fadd <6 x double> %a, %a
  store <6 x double> %a.add, ptr %A, align 8
  br label %end

false:
  %b.add = fadd <6 x double> %b, %b
  store <6 x double> %b.add, ptr %B, align 8
  br label %end

end:
  %a.2 = load <6 x double>, ptr %A, align 8
  %b.2 = load <6 x double>, ptr %B, align 8
  %c.2 = call <9 x double> @llvm.matrix.multiply(<6 x double> %a.2, <6 x double> %b.2, i32 3, i32 2, i32 3)
  store <9 x double> %c.2, ptr %C, align 8
  ret void
}

declare <9 x double> @llvm.matrix.multiply(<6 x double>, <6 x double>, i32, i32, i32)
