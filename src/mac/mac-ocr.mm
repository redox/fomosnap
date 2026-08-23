/** @fileoverview OCR through Vision. Replaces the tesseract subprocess: the
 *  recognizer ships with the OS, so there is no runtime dependency to install
 *  and no PNG round trip through a pipe. */

#include "mac-platform.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <Vision/Vision.h>

#include <QImage>

#include <algorithm>

namespace {

/// Wraps a QImage's pixels in a CGImage. Format_RGB32 is BGRX in memory on
/// little-endian, matching the bitmap layout requested here, so the
/// conversion is a copy rather than a per-pixel repack.
[[nodiscard]] CGImageRef createCGImage(const QImage &image) {
  QImage source = image.convertToFormat(QImage::Format_RGB32);
  if (source.isNull())
    return nullptr;
  CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(
      source.bits(), static_cast<size_t>(source.width()),
      static_cast<size_t>(source.height()), 8,
      static_cast<size_t>(source.bytesPerLine()), colorSpace,
      kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
  CGColorSpaceRelease(colorSpace);
  if (!context)
    return nullptr;
  CGImageRef result = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  return result;
}

} // namespace

namespace mac {

QString recognizeTextWithVision(const QImage &image,
                                const QStringList &languages, QString &error) {
  CGImageRef cgImage = createCGImage(image);
  if (!cgImage) {
    error = QStringLiteral("Could not prepare image for OCR");
    return {};
  }

  VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
  request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
  request.usesLanguageCorrection = YES;
  if (!languages.isEmpty()) {
    NSMutableArray<NSString *> *tags = [NSMutableArray array];
    for (const QString &language : languages)
      [tags addObject:language.toNSString()];
    request.recognitionLanguages = tags;
  }

  VNImageRequestHandler *handler =
      [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
  NSError *failure = nil;
  const BOOL performed = [handler performRequests:@[ request ] error:&failure];
  CGImageRelease(cgImage);
  if (!performed) {
    error = QStringLiteral("OCR failed: %1")
                .arg(failure ? QString::fromNSString(failure.localizedDescription)
                             : QStringLiteral("no detail reported"));
    return {};
  }

  // Vision returns observations in no guaranteed order, and its normalized
  // coordinates put the origin bottom-left, so reading order is descending y.
  NSArray<VNRecognizedTextObservation *> *observations = request.results;
  NSArray<VNRecognizedTextObservation *> *ordered = [observations
      sortedArrayUsingComparator:^NSComparisonResult(
          VNRecognizedTextObservation *left, VNRecognizedTextObservation *right) {
        const CGFloat leftTop = CGRectGetMaxY(left.boundingBox);
        const CGFloat rightTop = CGRectGetMaxY(right.boundingBox);
        if (std::abs(leftTop - rightTop) > 0.01)
          return leftTop > rightTop ? NSOrderedAscending : NSOrderedDescending;
        const CGFloat leftX = CGRectGetMinX(left.boundingBox);
        const CGFloat rightX = CGRectGetMinX(right.boundingBox);
        if (leftX == rightX)
          return NSOrderedSame;
        return leftX < rightX ? NSOrderedAscending : NSOrderedDescending;
      }];

  QStringList lines;
  for (VNRecognizedTextObservation *observation in ordered) {
    VNRecognizedText *best = [observation topCandidates:1].firstObject;
    if (best.string.length > 0)
      lines.append(QString::fromNSString(best.string));
  }

  const QString text = lines.join(QLatin1Char('\n')).trimmed();
  if (text.isEmpty())
    error = QStringLiteral("No text found in selection");
  return text;
}

} // namespace mac
